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

#include "src/containers/scatter_shuffle/scatter_shuffle_fn.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "gtest/gtest.h"
#include "src/containers/base/counter_names.h"
#include "src/crypto/release_encrypter.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/proto/scatter_shuffle.pb.h"
#include "src/testing/fake_context.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;
using FakeContext = ::cobalt::trusted_shuffler::testing::FakeCombinedContext;

constexpr absl::string_view kTestRunId = "test_run_id_01234567890123456789";
constexpr int32_t kDefaultProjectId = 100;
constexpr int32_t kDefaultMetricId = 200;
constexpr int64_t kDefaultEpochIndex = 42;

struct TestIngestionVerificationResultSpec {
  absl::string_view run_id = kTestRunId;
  int32_t target_batch_size = 5;
  int64_t min_epoch_index = 0;
  int64_t max_epoch_index = 100;
  int64_t message_count = 11;
  int32_t project_id = kDefaultProjectId;
  int32_t metric_id = kDefaultMetricId;
  int64_t epoch_index = kDefaultEpochIndex;
};

IngestionVerificationResult CreateIngestionVerificationResult(
    const TestIngestionVerificationResultSpec& spec) {
  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ABSL_CHECK_OK(release_key.status());

  IngestionVerificationResult result;

  RunContext run_context;
  run_context.set_run_id(std::string(spec.run_id));
  run_context.set_run_hash_key("hash_key_01234567890123456789012");
  run_context.set_release_key(*release_key);
  RunConfig* const run_config = run_context.mutable_run_config();
  run_config->set_target_batch_size(spec.target_batch_size);
  run_config->set_min_epoch_index(spec.min_epoch_index);
  run_config->set_max_epoch_index(spec.max_epoch_index);
  RunConfig::MetricIdentifier* const metric_identifier = run_config->add_metric_identifiers();
  metric_identifier->set_project_id(spec.project_id);
  metric_identifier->set_metric_id(spec.metric_id);

  SignedRunContext* const signed_run_context = result.mutable_signed_run_context();
  signed_run_context->set_serialized_run_context(run_context.SerializeAsString());
  signed_run_context->set_signature("fake_signature");

  result.mutable_threshold_report()->set_run_id(std::string(spec.run_id));
  MetricEpochKey* const approved_partition =
      result.mutable_threshold_report()->add_approved_partitions();
  approved_partition->set_project_id(spec.project_id);
  approved_partition->set_metric_id(spec.metric_id);
  approved_partition->set_epoch_index(spec.epoch_index);

  result.mutable_canonical_fingerprint()->set_run_id(std::string(spec.run_id));
  result.mutable_canonical_fingerprint()->set_stage_id(Stage::STAGE_INGESTION);
  result.mutable_canonical_fingerprint()->set_is_partial(false);
  FingerprintEntry* const entry = result.mutable_canonical_fingerprint()->add_entries();
  entry->mutable_metric_epoch_key()->set_project_id(spec.project_id);
  entry->mutable_metric_epoch_key()->set_metric_id(spec.metric_id);
  entry->mutable_metric_epoch_key()->set_epoch_index(spec.epoch_index);
  if (spec.message_count == 0) {
    entry->set_fingerprint(std::string(32, '\0'));
  } else {
    entry->set_fingerprint("fake_digest_32_bytes_long_012345");
  }
  entry->set_message_count(spec.message_count);

  return result;
}

IngestionVerificationPayload CreateIngestionVerificationPayload(
    const TestIngestionVerificationResultSpec& spec) {
  IngestionVerificationPayload payload;
  *payload.mutable_success() = CreateIngestionVerificationResult(spec);
  return payload;
}

ScatterShuffleFn::KV CreateConfigRecord(const IngestionVerificationPayload& payload) {
  return ScatterShuffleFn::KV(google::protobuf::Any(), payload.SerializeAsString());
}

struct TestMessageSpec {
  absl::string_view payload;
  bool is_padding = false;
  absl::string_view run_id = kTestRunId;
  int32_t shuffle_count = 0;
  int32_t project_id = kDefaultProjectId;
  int32_t metric_id = kDefaultMetricId;
  int64_t epoch_index = kDefaultEpochIndex;
};

ShuffledMessage CreateMessage(const TestMessageSpec& spec) {
  ShuffledMessage message;
  message.set_run_id(std::string(spec.run_id));
  message.set_customer_message(std::string(spec.payload));
  message.set_is_padding(spec.is_padding);
  message.set_shuffle_count(spec.shuffle_count);

  MetricEpochKey* const metric_epoch_key = message.mutable_metric_epoch_key();
  metric_epoch_key->set_project_id(spec.project_id);
  metric_epoch_key->set_metric_id(spec.metric_id);
  metric_epoch_key->set_epoch_index(spec.epoch_index);

  return message;
}

ScatterShuffleFn::KV CreateMessageRecord(const ShuffledMessage& message) {
  google::protobuf::Any key;
  key.PackFrom(message.metric_epoch_key());
  return ScatterShuffleFn::KV(std::move(key), message.SerializeAsString());
}

TEST(ScatterShuffleFnTest, SuccessfulScatterShuffleAndGroupAssignment) {
  FakeContext context;
  ScatterShuffleFn fn;

  // Ingest Record 0: IngestionVerificationPayload with target_batch_size = 6, message_count = 11.
  // Batch calculation:
  // batch_num = ceil(11 / 6) = 2
  // remainder_count = 11 % 2 = 1
  // padding_count = 2 - 1 = 1 (so 11 + 1 = 12 = 2 * 6)
  // actual_batch_size = 6
  // target_batch_size = 6
  const IngestionVerificationPayload verification_payload =
      CreateIngestionVerificationPayload({.target_batch_size = 6, .message_count = 11});
  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  // Ingest Records 1..6: 4 real messages + 2 padding messages.
  const std::vector<std::string> real_payloads = {"msg_0", "msg_1", "msg_2", "msg_3"};
  const std::vector<std::string> padding_payloads = {"pad_0", "pad_1"};

  for (const std::string& payload : real_payloads) {
    const ShuffledMessage message = CreateMessage({.payload = payload, .is_padding = false});
    ASSERT_THAT(fn.Do(CreateMessageRecord(message), context), IsOk());
  }
  for (const std::string& payload : padding_payloads) {
    const ShuffledMessage message = CreateMessage({.payload = payload, .is_padding = true});
    ASSERT_THAT(fn.Do(CreateMessageRecord(message), context), IsOk());
  }

  // Finalize replica session.
  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  // Verify privacy boundary and emission contracts:
  // 1. Errors: None.
  EXPECT_THAT(context.Errors(), IsEmpty());
  // 2. Unencrypted: Strictly EMPTY. No sensitive payloads or messages leaked in plaintext.
  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  // 3. Releasable: Empty.
  EXPECT_THAT(context.Releasable(), IsEmpty());
  // 4. Encrypted: Exactly 6 messages emitted under kReencryptionKeyIndex (0).
  const std::vector<std::pair<int, ScatterShuffleFn::KV>>& encrypted = context.Encrypted();
  ASSERT_THAT(encrypted, SizeIs(6));

  std::map<int32_t, int> group_counts;
  std::vector<std::string> emitted_real_payloads;
  std::vector<std::string> emitted_padding_payloads;

  for (const auto& [key_idx, kv] : encrypted) {
    EXPECT_EQ(key_idx, 0);

    // Verify visible unencrypted header.
    ScatterShuffleHeader header;
    ASSERT_TRUE(kv.key.UnpackTo(&header));
    ASSERT_EQ(header.header_type_case(), ScatterShuffleHeader::kScatterGroupKey);
    const ScatterGroupKey& group_key = header.scatter_group_key();
    EXPECT_EQ(group_key.metric_epoch_key().project_id(), kDefaultProjectId);
    EXPECT_EQ(group_key.metric_epoch_key().metric_id(), kDefaultMetricId);
    EXPECT_EQ(group_key.metric_epoch_key().epoch_index(), kDefaultEpochIndex);
    const int32_t group_id = group_key.group_id();
    EXPECT_GE(group_id, 0);
    EXPECT_LT(group_id, 2);
    group_counts[group_id]++;

    // Verify encrypted payload content.
    ShuffledMessage emitted_message;
    ASSERT_TRUE(emitted_message.ParseFromString(kv.data));
    EXPECT_EQ(emitted_message.run_id(), kTestRunId);
    EXPECT_EQ(emitted_message.shuffle_count(), 1);
    EXPECT_EQ(emitted_message.group_id(), group_id);
    EXPECT_EQ(emitted_message.metric_epoch_key().project_id(), kDefaultProjectId);
    EXPECT_EQ(emitted_message.metric_epoch_key().metric_id(), kDefaultMetricId);
    EXPECT_EQ(emitted_message.metric_epoch_key().epoch_index(), kDefaultEpochIndex);

    if (emitted_message.is_padding()) {
      emitted_padding_payloads.push_back(emitted_message.customer_message());
    } else {
      emitted_real_payloads.push_back(emitted_message.customer_message());
    }
  }

  // Multiset preservation checks.
  EXPECT_THAT(emitted_real_payloads, ::testing::UnorderedElementsAreArray(real_payloads));
  EXPECT_THAT(emitted_padding_payloads, ::testing::UnorderedElementsAreArray(padding_payloads));

  // Balanced modulo group distribution.
  EXPECT_EQ(group_counts[0], 3);
  EXPECT_EQ(group_counts[1], 3);

  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordCount)), Eq(6));
  EXPECT_THAT(context.GetCounters().at(std::string(kEncryptedOutputRecordCount)), Eq(6));
}

TEST(ScatterShuffleFnTest, BatchParametersEstablishedOnFirstDataRecord) {
  FakeContext context;
  ScatterShuffleFn fn;

  const IngestionVerificationPayload verification_payload =
      CreateIngestionVerificationPayload({.target_batch_size = 6, .message_count = 11});
  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  // Batch parameters are not established before any data records arrive.
  EXPECT_FALSE(fn.CurrentBatchParameters().has_value());

  // Record 1 arrives, establishing batch parameters for its partition.
  const ShuffledMessage message = CreateMessage({.payload = "msg_0"});
  ASSERT_THAT(fn.Do(CreateMessageRecord(message), context), IsOk());

  ASSERT_TRUE(fn.CurrentBatchParameters().has_value());
  EXPECT_EQ(fn.CurrentBatchParameters()->target_batch_size, 6);
  EXPECT_EQ(fn.CurrentBatchParameters()->actual_batch_size, 6);
  EXPECT_EQ(fn.CurrentBatchParameters()->batch_num, 2);
  EXPECT_EQ(fn.CurrentBatchParameters()->padding_count, 1);
}

TEST(ScatterShuffleFnTest, RejectsRecordWithUnapprovedPartition) {
  FakeContext context;
  ScatterShuffleFn fn;

  // IngestionVerificationPayload only approves (100, 200, 42).
  const IngestionVerificationPayload verification_payload =
      CreateIngestionVerificationPayload({.project_id = 100, .metric_id = 200, .epoch_index = 42});
  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  // Record has unapproved partition (999, 200, 42).
  const ShuffledMessage message = CreateMessage({
      .payload = "m1",
      .project_id = 999,
      .metric_id = 200,
      .epoch_index = 42,
  });
  EXPECT_THAT(fn.Do(CreateMessageRecord(message), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Failed to compute batch parameters for partition."));

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kUnencryptedOutputRecordCount)), Eq(1));
  ScatterShuffleHeader header;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&header));
  EXPECT_EQ(header.header_type_case(), ScatterShuffleHeader::kIntegrityFault);
}

TEST(ScatterShuffleFnTest, RejectsRecordWithMissingPartitionInFingerprint) {
  FakeContext context;
  ScatterShuffleFn fn;

  IngestionVerificationPayload verification_payload =
      CreateIngestionVerificationPayload({.project_id = 100, .metric_id = 200, .epoch_index = 42});
  // Clear canonical_fingerprint entries so partition is approved but has no fingerprint entry.
  verification_payload.mutable_success()->mutable_canonical_fingerprint()->clear_entries();

  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  const ShuffledMessage message = CreateMessage({.payload = "m1"});
  EXPECT_THAT(fn.Do(CreateMessageRecord(message), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Failed to compute batch parameters for partition."));

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  ScatterShuffleHeader header;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&header));
  EXPECT_EQ(header.header_type_case(), ScatterShuffleHeader::kIntegrityFault);
}

TEST(ScatterShuffleFnTest, RejectsRecordWithZeroMessageCountInFingerprint) {
  FakeContext context;
  ScatterShuffleFn fn;

  const IngestionVerificationPayload verification_payload = CreateIngestionVerificationPayload({
      .message_count = 0,
      .project_id = 100,
      .metric_id = 200,
      .epoch_index = 42,
  });
  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  const ShuffledMessage message = CreateMessage({.payload = "m1"});
  EXPECT_THAT(fn.Do(CreateMessageRecord(message), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Failed to compute batch parameters for partition."));

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  ScatterShuffleHeader header;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&header));
  EXPECT_EQ(header.header_type_case(), ScatterShuffleHeader::kIntegrityFault);
}

TEST(ScatterShuffleFnTest, RejectsEmptyBatchWithIntegrityFault) {
  FakeContext context;
  ScatterShuffleFn fn;

  const IngestionVerificationPayload verification_payload = CreateIngestionVerificationPayload({});
  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  // Finalize without sending any messages.
  EXPECT_THAT(
      fn.FinalizeReplica(google::protobuf::Any(), context),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "IntegrityFault: Batch parameters were not established (no records received)."));

  // Must emit unencrypted IntegrityFault header to alert the orchestrator.
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  ScatterShuffleHeader header;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&header));
  EXPECT_EQ(header.header_type_case(), ScatterShuffleHeader::kIntegrityFault);

  // No encrypted messages should be emitted.
  EXPECT_THAT(context.Encrypted(), IsEmpty());
}

TEST(ScatterShuffleFnTest, RejectsTooFewMessagesWithIntegrityFault) {
  FakeContext context;
  ScatterShuffleFn fn;

  // target_batch_size = 5, message_count = 5 -> batch_num = 1, target_batch_size = 5.
  const IngestionVerificationPayload verification_payload =
      CreateIngestionVerificationPayload({.target_batch_size = 5, .message_count = 5});
  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  // Send only 3 messages when 5 are expected.
  for (int i = 0; i < 3; ++i) {
    const ShuffledMessage message = CreateMessage({.payload = absl::StrCat("m", i)});
    ASSERT_THAT(fn.Do(CreateMessageRecord(message), context), IsOk());
  }

  EXPECT_THAT(
      fn.FinalizeReplica(google::protobuf::Any(), context),
      StatusIs(absl::StatusCode::kFailedPrecondition, "IntegrityFault: Batch size mismatch."));

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  ScatterShuffleHeader header;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&header));
  EXPECT_EQ(header.header_type_case(), ScatterShuffleHeader::kIntegrityFault);
  EXPECT_THAT(context.Encrypted(), IsEmpty());
}

TEST(ScatterShuffleFnTest, RejectsTooManyMessagesWithIntegrityFault) {
  FakeContext context;
  ScatterShuffleFn fn;

  // target_batch_size = 2, message_count = 2 -> batch_num = 1, target_batch_size = 2.
  const IngestionVerificationPayload verification_payload =
      CreateIngestionVerificationPayload({.target_batch_size = 2, .message_count = 2});
  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  ASSERT_THAT(fn.Do(CreateMessageRecord(CreateMessage({.payload = "m1"})), context), IsOk());
  ASSERT_THAT(fn.Do(CreateMessageRecord(CreateMessage({.payload = "m2"})), context), IsOk());

  // Sending a 3rd message must fail immediately on Do().
  EXPECT_THAT(fn.Do(CreateMessageRecord(CreateMessage({.payload = "m3"})), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Received more messages than expected batch size."));

  // Verify that an unencrypted IntegrityFault was emitted.
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  ScatterShuffleHeader header;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&header));
  EXPECT_EQ(header.header_type_case(), ScatterShuffleHeader::kIntegrityFault);

  // FinalizeReplica must also fail and not duplicate fault emission.
  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition, "Integrity fault already emitted."));
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));
  EXPECT_THAT(context.Encrypted(), IsEmpty());
}

TEST(ScatterShuffleFnTest, RejectsMessageWithMismatchedRunId) {
  FakeContext context;
  ScatterShuffleFn fn;

  const IngestionVerificationPayload verification_payload =
      CreateIngestionVerificationPayload({.run_id = "expected_run_id_0123456789012345"});
  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  const ShuffledMessage message = CreateMessage(
      {.payload = "m1", .is_padding = false, .run_id = "unexpected_run_id_01234567890123"});
  EXPECT_THAT(
      fn.Do(CreateMessageRecord(message), context),
      StatusIs(absl::StatusCode::kFailedPrecondition, "IntegrityFault: Message run_id mismatch."));

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  ScatterShuffleHeader header;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&header));
  EXPECT_EQ(header.header_type_case(), ScatterShuffleHeader::kIntegrityFault);
}

TEST(ScatterShuffleFnTest, RejectsMessageWithNonZeroShuffleCount) {
  FakeContext context;
  ScatterShuffleFn fn;

  const IngestionVerificationPayload verification_payload = CreateIngestionVerificationPayload({});
  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  const ShuffledMessage message =
      CreateMessage({.payload = "m1", .is_padding = false, .shuffle_count = 1});
  EXPECT_THAT(fn.Do(CreateMessageRecord(message), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Message shuffle_count must be 0."));

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  ScatterShuffleHeader header;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&header));
  EXPECT_EQ(header.header_type_case(), ScatterShuffleHeader::kIntegrityFault);
}

TEST(ScatterShuffleFnTest, RejectsMessageWithMismatchedMetricEpochKeyInBatch) {
  FakeContext context;
  ScatterShuffleFn fn;

  // IngestionVerificationPayload with multiple approved partitions.
  IngestionVerificationPayload verification_payload =
      CreateIngestionVerificationPayload({.target_batch_size = 5, .message_count = 5});
  MetricEpochKey* const approved2 =
      verification_payload.mutable_success()->mutable_threshold_report()->add_approved_partitions();
  approved2->set_project_id(10);
  approved2->set_metric_id(99);
  approved2->set_epoch_index(30);
  FingerprintEntry* const entry2 =
      verification_payload.mutable_success()->mutable_canonical_fingerprint()->add_entries();
  entry2->mutable_metric_epoch_key()->set_project_id(10);
  entry2->mutable_metric_epoch_key()->set_metric_id(99);
  entry2->mutable_metric_epoch_key()->set_epoch_index(30);
  entry2->set_fingerprint("fake_digest_32_bytes_long_012345");
  entry2->set_message_count(5);

  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  const ShuffledMessage message1 = CreateMessage({
      .payload = "m1",
      .project_id = kDefaultProjectId,
      .metric_id = kDefaultMetricId,
      .epoch_index = kDefaultEpochIndex,
  });
  ASSERT_THAT(fn.Do(CreateMessageRecord(message1), context), IsOk());

  // Second message has different partition.
  const ShuffledMessage message2 = CreateMessage({
      .payload = "m2",
      .project_id = 10,
      .metric_id = 99,
      .epoch_index = 30,
  });
  EXPECT_THAT(fn.Do(CreateMessageRecord(message2), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: MetricEpochKey mismatch in batch."));

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  ScatterShuffleHeader header;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&header));
  EXPECT_EQ(header.header_type_case(), ScatterShuffleHeader::kIntegrityFault);
}

TEST(ScatterShuffleFnTest, RejectsCorruptedShuffledMessagePayload) {
  FakeContext context;
  ScatterShuffleFn fn;

  const IngestionVerificationPayload verification_payload = CreateIngestionVerificationPayload({});
  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  MetricEpochKey header_key;
  header_key.set_project_id(kDefaultProjectId);
  header_key.set_metric_id(kDefaultMetricId);
  header_key.set_epoch_index(kDefaultEpochIndex);
  google::protobuf::Any header_any;
  header_any.PackFrom(header_key);

  ScatterShuffleFn::KV kv(std::move(header_any), "corrupted_payload_bytes");
  EXPECT_THAT(fn.Do(std::move(kv), context), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ScatterShuffleFnTest, RejectsMessageMissingMetricEpochKey) {
  FakeContext context;
  ScatterShuffleFn fn;

  const IngestionVerificationPayload verification_payload = CreateIngestionVerificationPayload({});
  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  ShuffledMessage message = CreateMessage({.payload = "m1"});
  message.clear_metric_epoch_key();

  MetricEpochKey header_key;
  header_key.set_project_id(kDefaultProjectId);
  header_key.set_metric_id(kDefaultMetricId);
  header_key.set_epoch_index(kDefaultEpochIndex);
  google::protobuf::Any header_any;
  header_any.PackFrom(header_key);

  ScatterShuffleFn::KV kv(std::move(header_any), message.SerializeAsString());
  EXPECT_THAT(fn.Do(std::move(kv), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Message missing required MetricEpochKey."));

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  ScatterShuffleHeader header;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&header));
  EXPECT_EQ(header.header_type_case(), ScatterShuffleHeader::kIntegrityFault);
}

TEST(ScatterShuffleFnTest, RejectsCorruptedFirstRecord) {
  FakeContext context;
  ScatterShuffleFn fn;

  EXPECT_THAT(fn.Do(ScatterShuffleFn::KV(google::protobuf::Any(), "corrupted_bytes"), context),
              StatusIs(absl::StatusCode::kInvalidArgument));

  // Subsequent record must be rejected with FailedPrecondition under one-shot semantics.
  const ShuffledMessage message = CreateMessage({.payload = "m1"});
  EXPECT_THAT(fn.Do(CreateMessageRecord(message), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ScatterShuffleFnTest, RejectsIntegrityFaultInPayload) {
  FakeContext context;
  ScatterShuffleFn fn;

  IngestionVerificationPayload payload;
  payload.mutable_integrity_fault();

  EXPECT_THAT(fn.Do(CreateConfigRecord(payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Ingestion verification failed with integrity fault."));

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigInvalid)), Eq(1));
  ScatterShuffleHeader header;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&header));
  EXPECT_EQ(header.header_type_case(), ScatterShuffleHeader::kIntegrityFault);
}

TEST(ScatterShuffleFnTest, RejectsEmptyRunIdInRunContext) {
  FakeContext context;
  ScatterShuffleFn fn;

  const IngestionVerificationPayload verification_payload =
      CreateIngestionVerificationPayload({.run_id = ""});
  EXPECT_THAT(fn.Do(CreateConfigRecord(verification_payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Failed to verify SignedRunContext."));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigInvalid)), Eq(1));
}

TEST(ScatterShuffleFnTest, RejectsMissingSignedRunContext) {
  FakeContext context;
  ScatterShuffleFn fn;

  IngestionVerificationPayload verification_payload;
  verification_payload.mutable_success()->mutable_threshold_report()->set_run_id("some_run_id");
  EXPECT_THAT(fn.Do(CreateConfigRecord(verification_payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: IngestionVerificationResult missing signed_run_context."));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigInvalid)), Eq(1));
}

TEST(ScatterShuffleFnTest, RejectsMissingThresholdReport) {
  FakeContext context;
  ScatterShuffleFn fn;

  IngestionVerificationPayload verification_payload = CreateIngestionVerificationPayload({});
  verification_payload.mutable_success()->clear_threshold_report();
  EXPECT_THAT(fn.Do(CreateConfigRecord(verification_payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: IngestionVerificationResult missing threshold_report."));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigInvalid)), Eq(1));
}

TEST(ScatterShuffleFnTest, RejectsMismatchedThresholdReportRunId) {
  FakeContext context;
  ScatterShuffleFn fn;

  IngestionVerificationPayload verification_payload = CreateIngestionVerificationPayload({});
  verification_payload.mutable_success()->mutable_threshold_report()->set_run_id(
      "mismatched_run_id_01234567890123");
  EXPECT_THAT(fn.Do(CreateConfigRecord(verification_payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: ThresholdReport run_id mismatch."));
}

TEST(ScatterShuffleFnTest, RejectsInvalidApprovedPartitionBounds) {
  FakeContext context;
  ScatterShuffleFn fn;

  IngestionVerificationPayload verification_payload = CreateIngestionVerificationPayload({});
  verification_payload.mutable_success()
      ->mutable_threshold_report()
      ->mutable_approved_partitions(0)
      ->set_project_id(0);
  EXPECT_THAT(fn.Do(CreateConfigRecord(verification_payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Invalid MetricEpochKey in approved_partitions."));
}

TEST(ScatterShuffleFnTest, RejectsInvalidTargetBatchSize) {
  FakeContext context;
  ScatterShuffleFn fn;

  const IngestionVerificationPayload verification_payload =
      CreateIngestionVerificationPayload({.target_batch_size = 0});
  EXPECT_THAT(fn.Do(CreateConfigRecord(verification_payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Failed to verify SignedRunContext."));
}

TEST(ScatterShuffleFnTest, RejectsFinalizeWithoutConfig) {
  FakeContext context;
  ScatterShuffleFn fn;

  EXPECT_THAT(
      fn.FinalizeReplica(google::protobuf::Any(), context),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "IntegrityFault: Configuration was not established before FinalizeReplica."));

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  ScatterShuffleHeader header;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&header));
  EXPECT_EQ(header.header_type_case(), ScatterShuffleHeader::kIntegrityFault);
}

TEST(ScatterShuffleFnTest, ShufflingPreservesMultisetAcrossLargeBatch) {
  FakeContext context;
  const int32_t batch_size = 50;
  const int32_t batch_num = 5;
  // target_batch_size = 50, message_count = 250 -> batch_num = 5, target_batch_size = 50.
  const IngestionVerificationPayload verification_payload =
      CreateIngestionVerificationPayload({.target_batch_size = 50, .message_count = 250});

  ScatterShuffleFn fn;
  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  std::vector<std::string> input_payloads;
  for (int i = 0; i < batch_size; ++i) {
    const std::string payload = absl::StrCat("payload_", i);
    input_payloads.push_back(payload);
    const ShuffledMessage message = CreateMessage({.payload = payload});
    ASSERT_THAT(fn.Do(CreateMessageRecord(message), context), IsOk());
  }

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  const std::vector<std::pair<int, ScatterShuffleFn::KV>>& encrypted = context.Encrypted();
  ASSERT_THAT(encrypted, SizeIs(batch_size));

  std::map<int32_t, int> group_counts;
  std::vector<std::string> output_payloads;
  for (const auto& [_, kv] : encrypted) {
    ScatterShuffleHeader header;
    ASSERT_TRUE(kv.key.UnpackTo(&header));
    ASSERT_EQ(header.header_type_case(), ScatterShuffleHeader::kScatterGroupKey);
    group_counts[header.scatter_group_key().group_id()]++;

    ShuffledMessage message;
    ASSERT_TRUE(message.ParseFromString(kv.data));
    output_payloads.push_back(message.customer_message());
  }

  // Verify all 5 groups have exactly 10 messages.
  for (int g = 0; g < batch_num; ++g) {
    EXPECT_EQ(group_counts[g], 10);
  }

  // Verify all items are preserved.
  EXPECT_THAT(output_payloads, ::testing::UnorderedElementsAreArray(input_payloads));
}

TEST(ScatterShuffleFnTest, ActualBatchSizeDiffersFromTargetBatchSize) {
  FakeContext context;
  ScatterShuffleFn fn;

  // target_batch_size = 10, message_count = 25.
  // batch_num = ceil(25 / 10) = 3.
  // remainder_count = 25 % 3 = 1.
  // padding_count = 3 - 1 = 2.
  // actual_batch_size = (25 + 2) / 3 = 9.
  const IngestionVerificationPayload verification_payload =
      CreateIngestionVerificationPayload({.target_batch_size = 10, .message_count = 25});
  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  // Ingest 9 messages (actual_batch_size).
  for (int i = 0; i < 9; ++i) {
    const ShuffledMessage message = CreateMessage({.payload = absl::StrCat("payload_", i)});
    ASSERT_THAT(fn.Do(CreateMessageRecord(message), context), IsOk());
  }

  ASSERT_TRUE(fn.CurrentBatchParameters().has_value());
  EXPECT_EQ(fn.CurrentBatchParameters()->target_batch_size, 10);
  EXPECT_EQ(fn.CurrentBatchParameters()->actual_batch_size, 9);
  EXPECT_EQ(fn.CurrentBatchParameters()->batch_num, 3);
  EXPECT_EQ(fn.CurrentBatchParameters()->padding_count, 2);

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());
  EXPECT_THAT(context.Encrypted(), SizeIs(9));
}

TEST(ScatterShuffleFnTest, RejectsMoreThanActualBatchSizeMessages) {
  FakeContext context;
  ScatterShuffleFn fn;

  // target_batch_size = 10, message_count = 25 -> actual_batch_size = 9.
  const IngestionVerificationPayload verification_payload =
      CreateIngestionVerificationPayload({.target_batch_size = 10, .message_count = 25});
  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  // Send exactly actual_batch_size (9) messages.
  for (int i = 0; i < 9; ++i) {
    const ShuffledMessage message = CreateMessage({.payload = absl::StrCat("payload_", i)});
    ASSERT_THAT(fn.Do(CreateMessageRecord(message), context), IsOk());
  }

  // Sending the 10th message must trigger an integrity fault because actual_batch_size is 9.
  const ShuffledMessage excess_message = CreateMessage({.payload = "excess"});
  EXPECT_THAT(fn.Do(CreateMessageRecord(excess_message), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       ::testing::HasSubstr("Received more messages than expected batch size.")));
}

TEST(ScatterShuffleFnTest, FinalizeReplicaFailsWhenReceivedFewerThanActualBatchSize) {
  FakeContext context;
  ScatterShuffleFn fn;

  // target_batch_size = 10, message_count = 25 -> actual_batch_size = 9.
  const IngestionVerificationPayload verification_payload =
      CreateIngestionVerificationPayload({.target_batch_size = 10, .message_count = 25});
  ASSERT_THAT(fn.Do(CreateConfigRecord(verification_payload), context), IsOk());

  // Send only 8 messages instead of 9.
  for (int i = 0; i < 8; ++i) {
    const ShuffledMessage message = CreateMessage({.payload = absl::StrCat("payload_", i)});
    ASSERT_THAT(fn.Do(CreateMessageRecord(message), context), IsOk());
  }

  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       ::testing::HasSubstr("Batch size mismatch.")));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
