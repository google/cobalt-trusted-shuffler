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

#include "src/containers/pre_padding/pre_padding_fn.h"

#include <string>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/containers/base/counter_names.h"
#include "src/crypto/verify_signed_run_context.h"
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
using ::cobalt::trusted_shuffler::testing::CreateValidSignedRunContext;
using ::cobalt::trusted_shuffler::testing::FakeContext;
using ::cobalt::trusted_shuffler::testing::FakeDoContext;
using ::cobalt::trusted_shuffler::testing::FingerprintSpec;
using ::cobalt::trusted_shuffler::testing::MetricEpochSpec;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;

namespace {

google::protobuf::Any PackToAny(const google::protobuf::Message& message) {
  google::protobuf::Any any;
  any.PackFrom(message);
  return any;
}

int64_t GetCounterValue(const FakeContext& context, std::string_view name) {
  auto& counters = context.GetCounters();
  if (counters.find(std::string(name)) == counters.end()) {
    return 0;
  }
  return counters.at(std::string(name));
}

}  // namespace

TEST(PrePaddingFnTest, EmitsPaddingMessagesForApprovedPartitions) {
  PrePaddingFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  MetricEpochKey partition1 =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 2, .epoch_index = 50});
  MetricEpochKey partition2 =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 3, .epoch_index = 50});

  // partition1 has count 33, target=10 -> batch_num = 4, remainder = 1, padding_count = 3.
  // partition2 has count 10, target=10 -> batch_num = 1, remainder = 0, padding_count = 0.
  auto run_context = testing::CreateValidRunContext(
      {{.project_id = 1001, .metric_id = 2}, {.project_id = 1001, .metric_id = 3}},
      /*min_epoch=*/0, /*max_epoch=*/100, /*batch_size=*/10);
  auto signed_run_context = CreateValidSignedRunContext(run_context);

  auto payload = CreateIngestionVerificationPayload(
      testing::kTestRunId,
      /*approved_partitions=*/{partition1, partition2},
      /*canonical_fingerprint=*/
      {{partition1, /*message_count=*/33}, {partition2, /*message_count=*/10}}, signed_run_context);

  // Input config
  EXPECT_THAT(fn.Do(PrePaddingFn::KV(payload.SerializeAsString()), do_context), IsOk());

  EXPECT_THAT(fn.FinalizeReplica(PackToAny(payload), do_context), IsOk());

  // Padding messages are encrypted to Node::kPaddingMessages, key index 0.
  auto encrypted_res = context.Encrypted();
  EXPECT_THAT(encrypted_res, SizeIs(3));
  for (const auto& output : encrypted_res) {
    EXPECT_THAT(output.first, Eq(0));

    MetricEpochKey emitted_key;
    EXPECT_TRUE(output.second.key.UnpackTo(&emitted_key));
    EXPECT_THAT(emitted_key.project_id(), Eq(1001));
    EXPECT_THAT(emitted_key.metric_id(), Eq(2));
    EXPECT_THAT(emitted_key.epoch_index(), Eq(50));

    ShuffledMessage emitted_value;
    EXPECT_TRUE(emitted_value.ParseFromString(output.second.data));
    EXPECT_TRUE(emitted_value.is_padding());
    EXPECT_THAT(emitted_value.run_id(), SizeIs(kRunIdByteLength));
    EXPECT_THAT(emitted_value.metric_epoch_key().project_id(), Eq(1001));
    EXPECT_THAT(emitted_value.metric_epoch_key().metric_id(), Eq(2));
    EXPECT_THAT(emitted_value.metric_epoch_key().epoch_index(), Eq(50));
  }

  auto unencrypted_res = context.Unencrypted();
  EXPECT_THAT(unencrypted_res, IsEmpty());
  EXPECT_THAT(context.Errors(), IsEmpty());

  EXPECT_THAT(GetCounterValue(context, kInputConfigCount), Eq(1));
  EXPECT_THAT(GetCounterValue(context, kInputConfigInvalid), Eq(0));
  EXPECT_THAT(GetCounterValue(context, kEncryptedOutputRecordCount), Eq(3));
  EXPECT_THAT(GetCounterValue(context, kUnencryptedOutputRecordCount), Eq(0));
}

TEST(PrePaddingFnTest, EmitsNoPaddingWhenMessageCountEvenlyDividesBatchNum) {
  PrePaddingFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  MetricEpochKey partition1 =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 2, .epoch_index = 50});

  auto signed_run_context =
      CreateValidSignedRunContext(/*project_id=*/1001, /*metric_id=*/2, testing::kTestRunId,
                                  /*min_epoch_index=*/0, /*max_epoch_index=*/100);

  auto payload = CreateIngestionVerificationPayload(
      testing::kTestRunId,
      /*approved_partitions=*/{partition1},
      /*canonical_fingerprint=*/{{partition1, /*message_count=*/10000}},  // Requires no padding
      signed_run_context);

  EXPECT_THAT(fn.Do(PrePaddingFn::KV(payload.SerializeAsString()), do_context), IsOk());
  EXPECT_THAT(fn.FinalizeReplica(PackToAny(payload), do_context), IsOk());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
}

TEST(PrePaddingFnTest, EmitsPaddingMessagesWhenPartitionHasNonZeroRemainder) {
  PrePaddingFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  MetricEpochKey partition1 =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 2, .epoch_index = 50});

  auto signed_run_context =
      CreateValidSignedRunContext(/*project_id=*/1001, /*metric_id=*/2, testing::kTestRunId,
                                  /*min_epoch_index=*/0, /*max_epoch_index=*/100);

  auto payload = CreateIngestionVerificationPayload(
      testing::kTestRunId,
      /*approved_partitions=*/{partition1},
      /*canonical_fingerprint=*/
      {{partition1,
        /*message_count=*/901}},  // batch_num = 10, remainder = 1 -> requires 9 padding messages
      signed_run_context);

  EXPECT_THAT(fn.Do(PrePaddingFn::KV(payload.SerializeAsString()), do_context), IsOk());
  EXPECT_THAT(fn.FinalizeReplica(PackToAny(payload), do_context), IsOk());
  EXPECT_THAT(context.Encrypted(), SizeIs(9));
}

void ExpectIntegrityFaultEmitted(const FakeContext& context) {
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  EXPECT_TRUE(context.Unencrypted()[0].key.type_url().empty());
  IntegrityFault fault;
  EXPECT_TRUE(fault.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_THAT(GetCounterValue(context, kUnencryptedOutputRecordCount), Eq(1));
  EXPECT_THAT(GetCounterValue(context, kIntegrityFaultCount), Eq(1));
}

TEST(PrePaddingFnTest, RejectsAnyTrailingRecordsAndEmitsIntegrityFault) {
  PrePaddingFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  auto signed_run_context =
      CreateValidSignedRunContext(/*project_id=*/1001, /*metric_id=*/2, testing::kTestRunId,
                                  /*min_epoch_index=*/0, /*max_epoch_index=*/100);

  auto payload =
      CreateIngestionVerificationPayload(testing::kTestRunId,
                                         /*approved_partitions=*/{},
                                         /*canonical_fingerprint=*/{}, signed_run_context);

  EXPECT_THAT(fn.Do(PrePaddingFn::KV(payload.SerializeAsString()), do_context), IsOk());

  EXPECT_THAT(fn.Do(PrePaddingFn::KV(ShuffledMessage().SerializeAsString()), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  ExpectIntegrityFaultEmitted(context);
  EXPECT_THAT(GetCounterValue(context, kInputRecordDropped), Eq(1));

  // Subsequent FinalizeReplica also fails with FailedPrecondition without emitting a second fault.
  EXPECT_THAT(fn.FinalizeReplica(PackToAny(payload), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  ExpectIntegrityFaultEmitted(context);
}

TEST(PrePaddingFnTest, FinalizeReplicaSucceedsWhenDoIsNeverCalled) {
  PrePaddingFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  IngestionVerificationPayload empty_payload;

  EXPECT_THAT(fn.FinalizeReplica(PackToAny(empty_payload), do_context), IsOk());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.Unencrypted(), IsEmpty());
}

TEST(PrePaddingFnTest, FinalizeReplicaReturnsErrorAndEmitsIntegrityFaultWhenConfigIsInvalid) {
  PrePaddingFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  IngestionVerificationPayload empty_payload;

  EXPECT_THAT(fn.Do(PrePaddingFn::KV(""), do_context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(fn.FinalizeReplica(PackToAny(empty_payload), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  ExpectIntegrityFaultEmitted(context);
}

TEST(PrePaddingFnTest, ReturnsErrorAndEmitsIntegrityFaultWhenPayloadIsInvalid) {
  PrePaddingFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  IngestionVerificationPayload invalid_payload;
  invalid_payload.mutable_integrity_fault();

  EXPECT_THAT(fn.Do(PrePaddingFn::KV(invalid_payload.SerializeAsString()), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  ExpectIntegrityFaultEmitted(context);
  EXPECT_THAT(GetCounterValue(context, kInputConfigCount), Eq(1));
  EXPECT_THAT(GetCounterValue(context, kInputConfigInvalid), Eq(1));
}

TEST(PrePaddingFnTest, EmitsIntegrityFaultWhenBatchParametersCannotBeComputed) {
  PrePaddingFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  const MetricEpochKey partition1 =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 2, .epoch_index = 50});
  const auto signed_run_context =
      CreateValidSignedRunContext(/*project_id=*/1001, /*metric_id=*/2, testing::kTestRunId,
                                  /*min_epoch_index=*/0, /*max_epoch_index=*/100);

  // Approved partition is missing from canonical_fingerprint, triggering an integrity fault.
  const auto payload =
      CreateIngestionVerificationPayload(testing::kTestRunId,
                                         /*approved_partitions=*/{partition1},
                                         /*canonical_fingerprint=*/{}, signed_run_context);

  EXPECT_THAT(fn.Do(PrePaddingFn::KV(payload.SerializeAsString()), do_context), IsOk());
  EXPECT_THAT(fn.FinalizeReplica(PackToAny(payload), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  ExpectIntegrityFaultEmitted(context);
}

TEST(PrePaddingFnTest, SuccessiveFaultsPreserveSingleEmissionInvariant) {
  PrePaddingFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  IngestionVerificationPayload invalid_payload;
  invalid_payload.mutable_integrity_fault();

  EXPECT_THAT(fn.Do(PrePaddingFn::KV(invalid_payload.SerializeAsString()), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_TRUE(fn.HasIntegrityFault());

  EXPECT_THAT(fn.FinalizeReplica(PackToAny(invalid_payload), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  ExpectIntegrityFaultEmitted(context);
}

TEST(PrePaddingFnTest, DoubleFinalizeReturnsFailedPrecondition) {
  PrePaddingFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  MetricEpochKey partition1 =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 2, .epoch_index = 50});
  auto signed_run_context =
      CreateValidSignedRunContext(/*project_id=*/1001, /*metric_id=*/2, testing::kTestRunId,
                                  /*min_epoch_index=*/0, /*max_epoch_index=*/100);
  auto payload = CreateIngestionVerificationPayload(testing::kTestRunId,
                                                    /*approved_partitions=*/{partition1},
                                                    /*canonical_fingerprint=*/
                                                    {{partition1, /*message_count=*/1000}},
                                                    signed_run_context);

  EXPECT_THAT(fn.Do(PrePaddingFn::KV(payload.SerializeAsString()), do_context), IsOk());
  EXPECT_THAT(fn.FinalizeReplica(PackToAny(payload), do_context), IsOk());
  EXPECT_TRUE(fn.IsFinalized());

  EXPECT_THAT(fn.FinalizeReplica(PackToAny(payload), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(PrePaddingFnTest, DoAfterFinalizeReturnsFailedPrecondition) {
  PrePaddingFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  MetricEpochKey partition1 =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 2, .epoch_index = 50});
  auto signed_run_context =
      CreateValidSignedRunContext(/*project_id=*/1001, /*metric_id=*/2, testing::kTestRunId,
                                  /*min_epoch_index=*/0, /*max_epoch_index=*/100);
  auto payload = CreateIngestionVerificationPayload(testing::kTestRunId,
                                                    /*approved_partitions=*/{partition1},
                                                    /*canonical_fingerprint=*/
                                                    {{partition1, /*message_count=*/1000}},
                                                    signed_run_context);

  EXPECT_THAT(fn.Do(PrePaddingFn::KV(payload.SerializeAsString()), do_context), IsOk());
  EXPECT_THAT(fn.FinalizeReplica(PackToAny(payload), do_context), IsOk());

  EXPECT_THAT(fn.Do(PrePaddingFn::KV(ShuffledMessage().SerializeAsString()), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
