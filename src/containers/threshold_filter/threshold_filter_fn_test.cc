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

#include "src/containers/threshold_filter/threshold_filter_fn.h"

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "gtest/gtest.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/fake_context.h"
#include "src/testing/matchers.h"
#include "src/testing/test_utils.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using KV = ::confidential_federated_compute::Session::KV;
using ::cobalt::trusted_shuffler::testing::CreateIntegrityFaultPayload;
using ::cobalt::trusted_shuffler::testing::CreateMetricEpochKey;
using ::cobalt::trusted_shuffler::testing::CreateShuffledMessage;
using ::cobalt::trusted_shuffler::testing::CreateThresholdReportPayload;
using ::cobalt::trusted_shuffler::testing::CreateValidRunContext;
using ::cobalt::trusted_shuffler::testing::CreateValidSignedRunContext;
using ::cobalt::trusted_shuffler::testing::EquivToProto;
using ::cobalt::trusted_shuffler::testing::FakeContext;
using ::cobalt::trusted_shuffler::testing::kTestRunId;
using ::cobalt::trusted_shuffler::testing::MetricEpochSpec;
using ::cobalt::trusted_shuffler::testing::MetricIdentifierSpec;
using ::testing::IsEmpty;
using ::testing::SizeIs;

KV CreateVerificationKV(const IngestionVerificationPayload& payload) {
  return KV(payload.SerializeAsString());
}

KV CreateMessageKV(const MetricEpochKey& key, const ShuffledMessage& msg) {
  google::protobuf::Any any_key;
  any_key.PackFrom(key);
  return KV(std::move(any_key), msg.SerializeAsString());
}

TEST(ThresholdFilterFnTest, InitializeReplicaReturnsOk) {
  FakeContext context;
  ThresholdFilterFn fn;

  EXPECT_THAT(fn.InitializeReplica(google::protobuf::Any(), context), IsOk());
}

TEST(ThresholdFilterFnTest, ConfigSucceedsWithValidVerificationPayload) {
  FakeContext context;
  ThresholdFilterFn fn;

  RunContext run_context = CreateValidRunContext({{10, 1}}, 100, 200, 50);
  SignedRunContext signed_context = CreateValidSignedRunContext(run_context);
  auto report_payload = CreateThresholdReportPayload(
      kTestRunId, {{.project_id = 10, .metric_id = 1, .epoch_index = 150}}, signed_context);

  ASSERT_THAT(fn.Do(CreateVerificationKV(report_payload), context), IsOk());
  EXPECT_TRUE(fn.HasConfig());
  EXPECT_TRUE(fn.HasVerificationReport());
  EXPECT_TRUE(fn.SignedRunContext().has_value());
  EXPECT_TRUE(fn.RunContext().has_value());
  EXPECT_TRUE(fn.VerificationPayload().has_value());
  EXPECT_FALSE(fn.HasIntegrityFault());
  EXPECT_EQ(fn.ApprovedPartitionCount(), 1);
}

TEST(ThresholdFilterFnTest, ConfigFailsWithEmptyPayload) {
  FakeContext context;
  ThresholdFilterFn fn;

  EXPECT_THAT(fn.Do(KV(""), context), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());

  // Subsequent record rejected under one-shot failure semantics.
  MetricEpochKey key = CreateMetricEpochKey({.project_id = 10, .metric_id = 1, .epoch_index = 150});
  ShuffledMessage msg = CreateShuffledMessage("payload");
  EXPECT_THAT(fn.Do(CreateMessageKV(key, msg), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ThresholdFilterFnTest, ConfigFailsWithCorruptPayload) {
  FakeContext context;
  ThresholdFilterFn fn;

  EXPECT_THAT(fn.Do(KV("\xff\xff\xff\xff"), context), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());

  // Subsequent record rejected under one-shot failure semantics.
  MetricEpochKey key = CreateMetricEpochKey({.project_id = 10, .metric_id = 1, .epoch_index = 150});
  ShuffledMessage msg = CreateShuffledMessage("payload");
  EXPECT_THAT(fn.Do(CreateMessageKV(key, msg), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ThresholdFilterFnTest, ConfigFailsWithIntegrityFault) {
  FakeContext context;
  ThresholdFilterFn fn;

  auto fault_payload = CreateIntegrityFaultPayload();
  EXPECT_THAT(fn.Do(CreateVerificationKV(fault_payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_TRUE(fn.HasIntegrityFault());

  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ThresholdFilterFnTest, ConfigFailsWithMissingSuccessAndFault) {
  FakeContext context;
  ThresholdFilterFn fn;

  IngestionVerificationPayload empty_payload;
  EXPECT_THAT(fn.Do(CreateVerificationKV(empty_payload), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());
}

TEST(ThresholdFilterFnTest, ConfigFailsWithMissingSignedRunContext) {
  FakeContext context;
  ThresholdFilterFn fn;

  IngestionVerificationPayload payload;
  auto* success = payload.mutable_success();
  success->mutable_threshold_report()->set_run_id(std::string(kTestRunId));

  EXPECT_THAT(fn.Do(CreateVerificationKV(payload), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());
}

TEST(ThresholdFilterFnTest, ConfigFailsWithEmptySignatureOnSignedRunContext) {
  FakeContext context;
  ThresholdFilterFn fn;

  RunContext run_context = CreateValidRunContext({{10, 1}}, 100, 200, 50);
  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature("");

  auto report_payload = CreateThresholdReportPayload(
      kTestRunId, {{.project_id = 10, .metric_id = 1, .epoch_index = 150}}, signed_context);

  EXPECT_THAT(fn.Do(CreateVerificationKV(report_payload), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());
}

TEST(ThresholdFilterFnTest, ConfigFailsWithInvalidRunContext) {
  FakeContext context;
  ThresholdFilterFn fn;

  RunContext run_context = CreateValidRunContext({{10, 1}}, 100, 200, 50);
  run_context.set_run_id("short_id");  // Invalid run_id length (not 32 bytes).
  SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  auto report_payload = CreateThresholdReportPayload(
      "short_id", {{.project_id = 10, .metric_id = 1, .epoch_index = 150}}, signed_context);

  EXPECT_THAT(fn.Do(CreateVerificationKV(report_payload), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());
}

TEST(ThresholdFilterFnTest, ConfigFailsWithRunIdMismatch) {
  FakeContext context;
  ThresholdFilterFn fn;

  RunContext run_context = CreateValidRunContext({{10, 1}}, 100, 200, 50);
  SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  // ThresholdReport specifies a different run ID than RunContext.
  auto report_payload = CreateThresholdReportPayload(
      "different_run_id_0123456789abcdef", {{.project_id = 10, .metric_id = 1, .epoch_index = 150}},
      signed_context);

  EXPECT_THAT(fn.Do(CreateVerificationKV(report_payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_FALSE(fn.HasConfig());
}

TEST(ThresholdFilterFnTest, FiltersMessagesForApprovedPartitions) {
  FakeContext context;
  ThresholdFilterFn fn;

  RunContext run_context = CreateValidRunContext({{10, 1}, {10, 2}}, 100, 200, 50);
  SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  // Record 0: Ingests IngestionVerificationPayload approving partition (10, 1, 150),
  // but NOT (10, 2, 150).
  auto report_payload = CreateThresholdReportPayload(
      kTestRunId, {{.project_id = 10, .metric_id = 1, .epoch_index = 150}}, signed_context);
  ASSERT_THAT(fn.Do(CreateVerificationKV(report_payload), context), IsOk());
  EXPECT_TRUE(fn.HasConfig());
  EXPECT_TRUE(fn.HasVerificationReport());
  EXPECT_EQ(fn.ApprovedPartitionCount(), 1);

  // Record 1: Send message for approved partition.
  MetricEpochKey approved_key =
      CreateMetricEpochKey({.project_id = 10, .metric_id = 1, .epoch_index = 150});
  ShuffledMessage msg1 = CreateShuffledMessage("payload_approved");
  ASSERT_THAT(fn.Do(CreateMessageKV(approved_key, msg1), context), IsOk());

  // Record 2: Send message for unapproved partition.
  MetricEpochKey unapproved_key =
      CreateMetricEpochKey({.project_id = 10, .metric_id = 2, .epoch_index = 150});
  ShuffledMessage msg2 = CreateShuffledMessage("payload_unapproved");
  ASSERT_THAT(fn.Do(CreateMessageKV(unapproved_key, msg2), context), IsOk());

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  // Verify: only the approved message was emitted, unencrypted output is strictly empty.
  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  ASSERT_THAT(context.Encrypted(), SizeIs(1));
  EXPECT_EQ(context.Encrypted().at(0).first, ThresholdFilterFn::kReencryptionKeyIndex);

  MetricEpochKey emitted_key;
  ASSERT_TRUE(context.Encrypted().at(0).second.key.UnpackTo(&emitted_key));
  EXPECT_THAT(emitted_key, EquivToProto(approved_key));

  ShuffledMessage emitted_msg;
  ASSERT_TRUE(emitted_msg.ParseFromString(context.Encrypted().at(0).second.data));
  EXPECT_EQ(emitted_msg.customer_message(), "payload_approved");
}

TEST(ThresholdFilterFnTest, FinalizeFailsWithoutConfig) {
  FakeContext context;
  ThresholdFilterFn fn;

  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ThresholdFilterFnTest, RejectsInvalidInputKVOnDataRecord) {
  FakeContext context;
  ThresholdFilterFn fn;

  RunContext run_context = CreateValidRunContext({{10, 1}}, 100, 200, 50);
  SignedRunContext signed_context = CreateValidSignedRunContext(run_context);
  auto report_payload = CreateThresholdReportPayload(
      kTestRunId, {{.project_id = 10, .metric_id = 1, .epoch_index = 150}}, signed_context);
  ASSERT_THAT(fn.Do(CreateVerificationKV(report_payload), context), IsOk());

  const KV invalid_kv(google::protobuf::Any(), "not_a_valid_shuffled_message");
  EXPECT_THAT(fn.Do(invalid_kv, context), StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
