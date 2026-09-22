// Copyright 2026 Google LLC.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/containers/gather_shuffle/gather_shuffle_fn.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "gtest/gtest.h"
#include "src/containers/base/counter_names.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/fake_context.h"
#include "src/testing/test_utils.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cobalt::trusted_shuffler::testing::CreateIngestionVerificationPayload;
using ::cobalt::trusted_shuffler::testing::CreateMetricEpochKey;
using ::cobalt::trusted_shuffler::testing::CreateValidRunContext;
using ::cobalt::trusted_shuffler::testing::CreateValidSignedRunContext;
using ::cobalt::trusted_shuffler::testing::kTestRunId;
using FakeContext = ::cobalt::trusted_shuffler::testing::FakeCombinedContext;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Ne;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAreArray;

constexpr int32_t kProjectId = 1001;
constexpr int32_t kMetricId = 2;
constexpr int64_t kEpochIndex = 50;

MetricEpochKey DefaultPartition() {
  return CreateMetricEpochKey(
      {.project_id = kProjectId, .metric_id = kMetricId, .epoch_index = kEpochIndex});
}

// The batching inputs a gather replica derives its expected group size from.
struct ConfigSpec {
  // Batch size the run was configured with, which fixes batch_num and
  // actual_batch_size for the partition.
  int32_t target_batch_size;

  // Messages the canonical ingestion fingerprint recorded for the partition.
  int64_t message_count;
};

IngestionVerificationPayload CreateConfig(const ConfigSpec& spec) {
  const MetricEpochKey partition = DefaultPartition();
  return CreateIngestionVerificationPayload(
      kTestRunId, /*approved_partitions=*/{partition},
      /*canonical_fingerprint=*/
      {{.metric_epoch_key = partition, .message_count = spec.message_count}},
      CreateValidSignedRunContext(
          CreateValidRunContext({{.project_id = kProjectId, .metric_id = kMetricId}},
                                /*min_epoch=*/0, /*max_epoch=*/100, spec.target_batch_size)));
}

// A scattered message arriving at a gather replica.
struct MessageSpec {
  // Customer payload carried through the second shuffle.
  absl::string_view payload;

  // Whether the message is dummy padding added by pre_padding.
  bool is_padding = false;

  // Run the message claims to belong to.
  absl::string_view run_id = kTestRunId;

  // Shuffle passes the message has been through. Scatter emits 1.
  int32_t shuffle_count = 1;

  // Gather group the message was assigned to by scatter.
  int32_t group_id = 0;

  // Partition the message belongs to.
  int32_t project_id = kProjectId;
  int32_t metric_id = kMetricId;
  int64_t epoch_index = kEpochIndex;

  // Whether to set metric_epoch_key at all.
  bool has_metric_epoch_key = true;
};

ShuffledMessage CreateMessage(const MessageSpec& spec) {
  ShuffledMessage message;
  message.set_customer_message(std::string(spec.payload));
  message.set_is_padding(spec.is_padding);
  message.set_run_id(std::string(spec.run_id));
  message.set_shuffle_count(spec.shuffle_count);
  message.set_group_id(spec.group_id);
  if (spec.has_metric_epoch_key) {
    *message.mutable_metric_epoch_key() = CreateMetricEpochKey({.project_id = spec.project_id,
                                                                .metric_id = spec.metric_id,
                                                                .epoch_index = spec.epoch_index});
  }
  return message;
}

// Gather replicas read keyless records: the orchestrator strips the
// ScatterShuffleHeader it routed on before handing records to the enclave.
GatherShuffleFn::KV CreateRecord(const google::protobuf::Message& message) {
  return GatherShuffleFn::KV(message.SerializeAsString());
}

google::protobuf::Any PackToAny(const google::protobuf::Message& message) {
  google::protobuf::Any any;
  any.PackFrom(message);
  return any;
}

int64_t GetCounterValue(const FakeContext& context, absl::string_view name) {
  const auto& counters = context.GetCounters();
  const auto it = counters.find(std::string(name));
  return it == counters.end() ? 0 : it->second;
}

TEST(GatherShuffleFnTest, GathersGroupAndEmitsItShuffledOnFinalize) {
  GatherShuffleFn fn;
  FakeContext context;

  // target_batch_size = 100 and message_count = 150 give batch_num = 2,
  // padding_count = 0 and actual_batch_size = 75. 75 % 2 = 1, so gather group 0
  // receives 2 * (75 / 2 + 1) = 76 messages and gather group 1 receives 74.
  const IngestionVerificationPayload config =
      CreateConfig({.target_batch_size = 100, .message_count = 150});
  ASSERT_THAT(fn.Do(CreateRecord(config), context), IsOk());
  EXPECT_THAT(GetCounterValue(context, kInputConfigCount), Eq(1));

  const int expected_group_size = 76;
  std::vector<std::string> real_payloads;
  std::vector<std::string> padding_payloads;
  for (int i = 0; i < expected_group_size; ++i) {
    const bool is_padding = i % 10 == 0;
    const std::string payload = absl::StrCat(is_padding ? "pad_" : "msg_", i);
    ASSERT_THAT(
        fn.Do(CreateRecord(CreateMessage({.payload = payload, .is_padding = is_padding})), context),
        IsOk());
    (is_padding ? padding_payloads : real_payloads).push_back(payload);
  }

  ASSERT_THAT(fn.FinalizeReplica(PackToAny(config), context), IsOk());

  // Nothing about the group escapes the enclave in plaintext.
  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  EXPECT_THAT(context.Releasable(), IsEmpty());
  EXPECT_THAT(context.Errors(), IsEmpty());

  const std::vector<std::pair<int, GatherShuffleFn::KV>> encrypted = context.Encrypted();
  ASSERT_THAT(encrypted, SizeIs(expected_group_size));

  std::vector<std::string> emitted_real_payloads;
  std::vector<std::string> emitted_padding_payloads;
  for (const auto& [key_index, kv] : encrypted) {
    // Encrypted to Node::kGatheredMessages, which is re-encryption key index 0.
    EXPECT_THAT(key_index, Eq(0));
    // The orchestrator routes gathered messages without a key; encrypt_output
    // reads the partition out of the decrypted payload.
    EXPECT_THAT(kv.key.type_url(), IsEmpty());

    ShuffledMessage emitted;
    ASSERT_TRUE(emitted.ParseFromString(kv.data));
    EXPECT_THAT(emitted.shuffle_count(), Eq(2));
    EXPECT_THAT(emitted.group_id(), Eq(0));
    EXPECT_THAT(emitted.run_id(), Eq(kTestRunId));
    EXPECT_TRUE(emitted.has_metric_epoch_key());
    EXPECT_THAT(emitted.metric_epoch_key().project_id(), Eq(kProjectId));
    EXPECT_THAT(emitted.metric_epoch_key().metric_id(), Eq(kMetricId));
    EXPECT_THAT(emitted.metric_epoch_key().epoch_index(), Eq(kEpochIndex));

    (emitted.is_padding() ? emitted_padding_payloads : emitted_real_payloads)
        .push_back(emitted.customer_message());
  }

  // The shuffle reorders the group without adding, dropping, or altering any
  // message, and padding stays marked as padding.
  EXPECT_THAT(emitted_real_payloads, UnorderedElementsAreArray(real_payloads));
  EXPECT_THAT(emitted_padding_payloads, UnorderedElementsAreArray(padding_payloads));

  EXPECT_THAT(GetCounterValue(context, kInputRecordCount), Eq(expected_group_size));
  EXPECT_THAT(GetCounterValue(context, kInputRecordDropped), Eq(0));
  EXPECT_THAT(GetCounterValue(context, kEncryptedOutputRecordCount), Eq(expected_group_size));
  EXPECT_THAT(GetCounterValue(context, kUnencryptedOutputRecordCount), Eq(0));
}

TEST(GatherShuffleFnTest, ShufflePermutesTheGatherGroup) {
  GatherShuffleFn fn;
  FakeContext context;

  // target_batch_size = 500 and message_count = 1000 give batch_num = 2 and
  // actual_batch_size = 500, so gather group 0 receives 2 * 250 = 500 messages.
  ASSERT_THAT(
      fn.Do(CreateRecord(CreateConfig({.target_batch_size = 500, .message_count = 1000})), context),
      IsOk());

  const int expected_group_size = 500;
  std::vector<std::string> input_order;
  for (int i = 0; i < expected_group_size; ++i) {
    const std::string payload = absl::StrCat("msg_", i);
    ASSERT_THAT(fn.Do(CreateRecord(CreateMessage({.payload = payload})), context), IsOk());
    input_order.push_back(payload);
  }

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  std::vector<std::string> emitted_order;
  for (const auto& [key_index, kv] : context.Encrypted()) {
    ShuffledMessage emitted;
    ASSERT_TRUE(emitted.ParseFromString(kv.data));
    emitted_order.push_back(emitted.customer_message());
  }

  // Every message survives, but the arrival order does not: leaving the group
  // in arrival order would leak how the scatter batches were composed, and the
  // chance of a uniform shuffle of 500 messages reproducing it is negligible.
  EXPECT_THAT(emitted_order, UnorderedElementsAreArray(input_order));
  EXPECT_THAT(emitted_order, Ne(input_order));
}

TEST(GatherShuffleFnTest, RejectsMessageWithMismatchedRunId) {
  GatherShuffleFn fn;
  FakeContext context;

  ASSERT_THAT(
      fn.Do(CreateRecord(CreateConfig({.target_batch_size = 100, .message_count = 150})), context),
      IsOk());

  EXPECT_THAT(fn.Do(CreateRecord(CreateMessage(
                        {.payload = "msg_0", .run_id = "ffffffffffffffffffffffffffffffff"})),
                    context),
              StatusIs(absl::StatusCode::kFailedPrecondition, HasSubstr("run_id mismatch")));
  EXPECT_THAT(GetCounterValue(context, kInputRecordDropped), Eq(1));
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));
  EXPECT_THAT(context.Encrypted(), IsEmpty());
}

TEST(GatherShuffleFnTest, RejectsMessageThatHasNotBeenScatterShuffledExactlyOnce) {
  for (const int32_t shuffle_count : {0, 2}) {
    GatherShuffleFn fn;
    FakeContext context;

    ASSERT_THAT(fn.Do(CreateRecord(CreateConfig({.target_batch_size = 100, .message_count = 150})),
                      context),
                IsOk());

    EXPECT_THAT(
        fn.Do(CreateRecord(CreateMessage({.payload = "msg_0", .shuffle_count = shuffle_count})),
              context),
        StatusIs(absl::StatusCode::kFailedPrecondition, HasSubstr("shuffle_count must be 1")));
    EXPECT_THAT(context.Encrypted(), IsEmpty());
  }
}

TEST(GatherShuffleFnTest, RejectsMessageMissingMetricEpochKey) {
  GatherShuffleFn fn;
  FakeContext context;

  ASSERT_THAT(
      fn.Do(CreateRecord(CreateConfig({.target_batch_size = 100, .message_count = 150})), context),
      IsOk());

  EXPECT_THAT(
      fn.Do(CreateRecord(CreateMessage({.payload = "msg_0", .has_metric_epoch_key = false})),
            context),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               HasSubstr("missing required MetricEpochKey")));
  EXPECT_THAT(GetCounterValue(context, kInputRecordDropped), Eq(1));
}

TEST(GatherShuffleFnTest, RejectsMessageFromAnotherPartition) {
  GatherShuffleFn fn;
  FakeContext context;

  ASSERT_THAT(
      fn.Do(CreateRecord(CreateConfig({.target_batch_size = 100, .message_count = 150})), context),
      IsOk());
  ASSERT_THAT(fn.Do(CreateRecord(CreateMessage({.payload = "msg_0"})), context), IsOk());

  // A gather group holds exactly one partition; the orchestrator mixing two
  // partitions into one replica is an integrity fault.
  EXPECT_THAT(
      fn.Do(CreateRecord(CreateMessage({.payload = "msg_1", .epoch_index = kEpochIndex + 1})),
            context),
      StatusIs(absl::StatusCode::kFailedPrecondition, HasSubstr("MetricEpochKey mismatch")));
  EXPECT_THAT(GetCounterValue(context, kInputRecordDropped), Eq(1));
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));
}

TEST(GatherShuffleFnTest, RejectsMessageFromAnotherGatherGroup) {
  GatherShuffleFn fn;
  FakeContext context;

  ASSERT_THAT(
      fn.Do(CreateRecord(CreateConfig({.target_batch_size = 100, .message_count = 150})), context),
      IsOk());
  ASSERT_THAT(fn.Do(CreateRecord(CreateMessage({.payload = "msg_0", .group_id = 0})), context),
              IsOk());

  // Mixing gather groups would undo the balanced scatter assignment the
  // expected group size is derived from.
  EXPECT_THAT(fn.Do(CreateRecord(CreateMessage({.payload = "msg_1", .group_id = 1})), context),
              StatusIs(absl::StatusCode::kFailedPrecondition, HasSubstr("group_id mismatch")));
  EXPECT_THAT(GetCounterValue(context, kInputRecordDropped), Eq(1));
}

TEST(GatherShuffleFnTest, RejectsFirstMessageFromAnUnapprovedPartition) {
  GatherShuffleFn fn;
  FakeContext context;

  ASSERT_THAT(
      fn.Do(CreateRecord(CreateConfig({.target_batch_size = 100, .message_count = 150})), context),
      IsOk());

  // Only DefaultPartition() cleared the k-anonymity threshold, so a replica
  // raised for any other partition has no expected group size to check against.
  EXPECT_THAT(
      fn.Do(CreateRecord(CreateMessage({.payload = "msg_0", .metric_id = kMetricId + 1})), context),
      StatusIs(absl::StatusCode::kFailedPrecondition, HasSubstr("not approved")));
  EXPECT_THAT(GetCounterValue(context, kInputRecordDropped), Eq(1));
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));
}

TEST(GatherShuffleFnTest, RejectsFirstMessageWithGroupIdOutsideBatchRange) {
  GatherShuffleFn fn;
  FakeContext context;

  // batch_num = 2, so the only gather groups are 0 and 1.
  ASSERT_THAT(
      fn.Do(CreateRecord(CreateConfig({.target_batch_size = 100, .message_count = 150})), context),
      IsOk());

  EXPECT_THAT(fn.Do(CreateRecord(CreateMessage({.payload = "msg_0", .group_id = 2})), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("group_id must be in [0, batch_num)")));
  EXPECT_THAT(GetCounterValue(context, kInputRecordDropped), Eq(1));
}

TEST(GatherShuffleFnTest, RejectsRecordBeyondTheExpectedGatherGroupSize) {
  GatherShuffleFn fn;
  FakeContext context;

  // batch_num = 3, actual_batch_size = 4, 4 % 3 = 1, so gather group 1 receives
  // 3 * (4 / 3) = 3 messages.
  ASSERT_THAT(
      fn.Do(CreateRecord(CreateConfig({.target_batch_size = 4, .message_count = 10})), context),
      IsOk());

  const int expected_group_size = 3;
  for (int i = 0; i < expected_group_size; ++i) {
    ASSERT_THAT(
        fn.Do(CreateRecord(CreateMessage({.payload = absl::StrCat("msg_", i), .group_id = 1})),
              context),
        IsOk());
  }

  // The record that takes the group past its expected size is rejected on
  // arrival rather than at finalize.
  EXPECT_THAT(fn.Do(CreateRecord(CreateMessage({.payload = "excess", .group_id = 1})), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("more messages than expected gather group size")));
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));
}

TEST(GatherShuffleFnTest, FinalizeFailsWhenTheGatherGroupIsIncomplete) {
  GatherShuffleFn fn;
  FakeContext context;

  // Gather group 0 owes 76 messages.
  ASSERT_THAT(
      fn.Do(CreateRecord(CreateConfig({.target_batch_size = 100, .message_count = 150})), context),
      IsOk());

  for (int i = 0; i < 75; ++i) {
    ASSERT_THAT(fn.Do(CreateRecord(CreateMessage({.payload = absl::StrCat("msg_", i)})), context),
                IsOk());
  }

  // An orchestrator that drops a message from the group must not be able to
  // have the rest released.
  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition, HasSubstr("group size mismatch")));
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));
}

TEST(GatherShuffleFnTest, FinalizeFailsWhenNoMessagesFollowTheConfig) {
  GatherShuffleFn fn;
  FakeContext context;

  ASSERT_THAT(
      fn.Do(CreateRecord(CreateConfig({.target_batch_size = 100, .message_count = 150})), context),
      IsOk());

  // A replica learns which gather group it serves from its first message, so an
  // empty one cannot account for the group it was raised for and faults. The
  // orchestrator routes by observed messages and so never raises one; see the
  // contract on GatherShuffleFn.
  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("Group parameters were not established")));
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));
}

TEST(GatherShuffleFnTest, RejectsCorruptedMessagePayload) {
  GatherShuffleFn fn;
  FakeContext context;

  ASSERT_THAT(
      fn.Do(CreateRecord(CreateConfig({.target_batch_size = 100, .message_count = 150})), context),
      IsOk());

  EXPECT_THAT(fn.Do(GatherShuffleFn::KV("\xff\xff corrupted payload bytes"), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(GatherShuffleFnTest, RejectsMessageSentInPlaceOfTheConfig) {
  GatherShuffleFn fn;
  FakeContext context;

  // The first record is always read as the configuration, so a message sent in
  // its place is rejected as an unparseable config rather than gathered.
  EXPECT_THAT(fn.Do(CreateRecord(CreateMessage({.payload = "msg_0"})), context),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Failed to parse first record as configuration proto")));
  EXPECT_THAT(context.Encrypted(), IsEmpty());
}

TEST(GatherShuffleFnTest, RejectsInvalidVerificationPayload) {
  GatherShuffleFn fn;
  FakeContext context;

  IngestionVerificationPayload payload;
  payload.mutable_integrity_fault();
  EXPECT_THAT(fn.Do(CreateRecord(payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(GetCounterValue(context, kInputConfigCount), Eq(1));
  EXPECT_THAT(GetCounterValue(context, kInputConfigInvalid), Eq(1));
  EXPECT_THAT(GetCounterValue(context, kUnencryptedOutputRecordCount), Eq(1));
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));
}

TEST(GatherShuffleFnTest, FinalizeFailsWhenConfigWasNeverReceived) {
  GatherShuffleFn fn;
  FakeContext context;

  EXPECT_THAT(fn.FinalizeReplica(PackToAny(IngestionVerificationPayload()), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       HasSubstr("Configuration was not established")));
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));
}

TEST(GatherShuffleFnTest, MultipleFaultsAcrossLifecycleEmitSingleIntegrityFaultRecord) {
  GatherShuffleFn fn;
  FakeContext context;

  IngestionVerificationPayload invalid_payload;
  invalid_payload.mutable_integrity_fault();

  EXPECT_THAT(fn.Do(CreateRecord(invalid_payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));

  // Records that arrive after a fault are rejected without a second fault
  // record, and so is finalize.
  EXPECT_THAT(fn.Do(CreateRecord(CreateMessage({.payload = "msg_0"})), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(fn.FinalizeReplica(PackToAny(invalid_payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  EXPECT_THAT(context.Unencrypted(), SizeIs(1));
  EXPECT_THAT(GetCounterValue(context, kUnencryptedOutputRecordCount), Eq(1));
  EXPECT_THAT(context.Encrypted(), IsEmpty());
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
