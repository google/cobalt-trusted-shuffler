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
#include "src/containers/base/counter_names.h"
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
using ::cobalt::trusted_shuffler::testing::CreateIntegrityFaultPayload;
using ::cobalt::trusted_shuffler::testing::CreateMetricEpochKey;
using ::cobalt::trusted_shuffler::testing::CreateShuffledMessage;
using ::cobalt::trusted_shuffler::testing::CreateThresholdReportPayload;
using ::cobalt::trusted_shuffler::testing::CreateValidRunContext;
using ::cobalt::trusted_shuffler::testing::CreateValidSignedRunContext;
using ::cobalt::trusted_shuffler::testing::EquivToProto;
using ::cobalt::trusted_shuffler::testing::kTestRunId;
using ::cobalt::trusted_shuffler::testing::MetricEpochSpec;
using ::cobalt::trusted_shuffler::testing::MetricIdentifierSpec;
using ::testing::Eq;
using FakeContext = ::cobalt::trusted_shuffler::testing::FakeCombinedContext;
using KV = ::confidential_federated_compute::Session::KV;
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
  EXPECT_EQ(fn.ApprovedPartitionCount(), 1);
}

TEST(ThresholdFilterFnTest, ConfigFailsWithEmptyPayload) {
  FakeContext context;
  ThresholdFilterFn fn;

  EXPECT_THAT(fn.Do(KV(""), context), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());

  // Subsequent record rejected under one-shot failure semantics.
  MetricEpochKey key = CreateMetricEpochKey({.project_id = 10, .metric_id = 1, .epoch_index = 150});
  ShuffledMessage msg = CreateShuffledMessage({
      .payload = "payload",
      .run_id = kTestRunId,
      .metric_epoch_key = key,
      .shuffle_count = 0,
  });
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
  ShuffledMessage msg = CreateShuffledMessage({
      .payload = "payload",
      .run_id = kTestRunId,
      .metric_epoch_key = key,
      .shuffle_count = 0,
  });
  EXPECT_THAT(fn.Do(CreateMessageKV(key, msg), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ThresholdFilterFnTest, ConfigFailsWithIntegrityFault) {
  FakeContext context;
  ThresholdFilterFn fn;

  auto fault_payload = CreateIntegrityFaultPayload();
  EXPECT_THAT(fn.Do(CreateVerificationKV(fault_payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  IntegrityFault fault;
  EXPECT_TRUE(context.Unencrypted().at(0).key.UnpackTo(&fault));

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

TEST(ThresholdFilterFnTest, ConfigFailsWithMissingThresholdReport) {
  FakeContext context;
  ThresholdFilterFn fn;

  RunContext run_context = CreateValidRunContext({{10, 1}}, 100, 200, 50);
  SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  IngestionVerificationPayload payload;
  auto* success = payload.mutable_success();
  *success->mutable_signed_run_context() = signed_context;

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
  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigInvalid)), Eq(1));
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
  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigInvalid)), Eq(1));
}

TEST(ThresholdFilterFnTest, ConfigFailsWithInvalidApprovedPartitionBounds) {
  FakeContext context;
  ThresholdFilterFn fn;

  RunContext run_context = CreateValidRunContext({{10, 1}}, 100, 200, 50);
  SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  auto report_payload = CreateThresholdReportPayload(
      kTestRunId, {{.project_id = 0, .metric_id = 1, .epoch_index = 150}}, signed_context);

  EXPECT_THAT(fn.Do(CreateVerificationKV(report_payload), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());
  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigInvalid)), Eq(1));
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
  ShuffledMessage msg1 = CreateShuffledMessage({
      .payload = "payload_approved",
      .run_id = kTestRunId,
      .metric_epoch_key = approved_key,
      .shuffle_count = 0,
  });
  ASSERT_THAT(fn.Do(CreateMessageKV(approved_key, msg1), context), IsOk());

  // Record 2: Send message for unapproved partition.
  MetricEpochKey unapproved_key =
      CreateMetricEpochKey({.project_id = 10, .metric_id = 2, .epoch_index = 150});
  ShuffledMessage msg2 = CreateShuffledMessage({
      .payload = "payload_unapproved",
      .run_id = kTestRunId,
      .metric_epoch_key = unapproved_key,
      .shuffle_count = 0,
  });
  ASSERT_THAT(fn.Do(CreateMessageKV(unapproved_key, msg2), context), IsOk());

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  // Verify: only the approved message was emitted, unencrypted output is strictly empty.
  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  ASSERT_THAT(context.Encrypted(), SizeIs(1));
  EXPECT_EQ(context.Encrypted().at(0).first, 0);

  MetricEpochKey emitted_key;
  ASSERT_TRUE(context.Encrypted().at(0).second.key.UnpackTo(&emitted_key));
  EXPECT_THAT(emitted_key, EquivToProto(approved_key));

  ShuffledMessage emitted_msg;
  ASSERT_TRUE(emitted_msg.ParseFromString(context.Encrypted().at(0).second.data));
  EXPECT_EQ(emitted_msg.customer_message(), "payload_approved");
  EXPECT_EQ(emitted_msg.run_id(), kTestRunId);
  EXPECT_EQ(emitted_msg.shuffle_count(), 0);
  EXPECT_THAT(emitted_msg.metric_epoch_key(), EquivToProto(approved_key));

  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordCount)), Eq(2));
  EXPECT_THAT(context.GetCounters().at(std::string(kEncryptedOutputRecordCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordDropped)), Eq(1));
}

TEST(ThresholdFilterFnTest, ForwardsApprovedPartitionWithStrippedHeaderKey) {
  FakeContext context;
  ThresholdFilterFn fn;

  RunContext run_context = CreateValidRunContext({{10, 1}}, 100, 200, 50);
  SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  auto report_payload = CreateThresholdReportPayload(
      kTestRunId, {{.project_id = 10, .metric_id = 1, .epoch_index = 150}}, signed_context);
  ASSERT_THAT(fn.Do(CreateVerificationKV(report_payload), context), IsOk());

  MetricEpochKey approved_key =
      CreateMetricEpochKey({.project_id = 10, .metric_id = 1, .epoch_index = 150});
  ShuffledMessage msg = CreateShuffledMessage({
      .payload = "payload_stripped_header",
      .run_id = kTestRunId,
      .metric_epoch_key = approved_key,
      .shuffle_count = 0,
  });

  // Upstream Stage 4 (GroupByMetricEpochKey) strips the header key, passing KV with empty key.
  KV stripped_record(msg.SerializeAsString());
  ASSERT_THAT(fn.Do(std::move(stripped_record), context), IsOk());

  ASSERT_THAT(context.Encrypted(), SizeIs(1));
  MetricEpochKey emitted_key;
  ASSERT_TRUE(context.Encrypted().at(0).second.key.UnpackTo(&emitted_key));
  EXPECT_THAT(emitted_key, EquivToProto(approved_key));
}

TEST(ThresholdFilterFnTest, OnRecordRejectsMissingMetricEpochKey) {
  FakeContext context;
  ThresholdFilterFn fn;

  RunContext run_context = CreateValidRunContext({{10, 1}}, 100, 200, 50);
  SignedRunContext signed_context = CreateValidSignedRunContext(run_context);
  auto report_payload = CreateThresholdReportPayload(
      kTestRunId, {{.project_id = 10, .metric_id = 1, .epoch_index = 150}}, signed_context);
  ASSERT_THAT(fn.Do(CreateVerificationKV(report_payload), context), IsOk());

  ShuffledMessage msg;
  msg.set_customer_message("missing_key");
  msg.set_run_id(std::string(kTestRunId));
  msg.set_shuffle_count(0);

  EXPECT_THAT(fn.Do(KV(msg.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordDropped)), Eq(1));
}

TEST(ThresholdFilterFnTest, OnRecordRejectsRunIdMismatch) {
  FakeContext context;
  ThresholdFilterFn fn;

  RunContext run_context = CreateValidRunContext({{10, 1}}, 100, 200, 50);
  SignedRunContext signed_context = CreateValidSignedRunContext(run_context);
  auto report_payload = CreateThresholdReportPayload(
      kTestRunId, {{.project_id = 10, .metric_id = 1, .epoch_index = 150}}, signed_context);
  ASSERT_THAT(fn.Do(CreateVerificationKV(report_payload), context), IsOk());

  MetricEpochKey approved_key =
      CreateMetricEpochKey({.project_id = 10, .metric_id = 1, .epoch_index = 150});
  ShuffledMessage msg = CreateShuffledMessage({
      .payload = "wrong_run_id",
      .run_id = "different_run_id_0123456789abcdef",
      .metric_epoch_key = approved_key,
      .shuffle_count = 0,
  });

  EXPECT_THAT(fn.Do(KV(msg.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  IntegrityFault fault;
  EXPECT_TRUE(context.Unencrypted().at(0).key.UnpackTo(&fault));
  EXPECT_TRUE(fault.ParseFromString(context.Unencrypted().at(0).data));
}

TEST(ThresholdFilterFnTest, OnRecordRejectsNonZeroShuffleCount) {
  FakeContext context;
  ThresholdFilterFn fn;

  RunContext run_context = CreateValidRunContext({{10, 1}}, 100, 200, 50);
  SignedRunContext signed_context = CreateValidSignedRunContext(run_context);
  auto report_payload = CreateThresholdReportPayload(
      kTestRunId, {{.project_id = 10, .metric_id = 1, .epoch_index = 150}}, signed_context);
  ASSERT_THAT(fn.Do(CreateVerificationKV(report_payload), context), IsOk());

  MetricEpochKey approved_key =
      CreateMetricEpochKey({.project_id = 10, .metric_id = 1, .epoch_index = 150});
  ShuffledMessage msg = CreateShuffledMessage({
      .payload = "already_shuffled",
      .run_id = kTestRunId,
      .metric_epoch_key = approved_key,
      .shuffle_count = 1,
  });

  EXPECT_THAT(fn.Do(KV(msg.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  IntegrityFault fault;
  EXPECT_TRUE(context.Unencrypted().at(0).key.UnpackTo(&fault));
  EXPECT_TRUE(fault.ParseFromString(context.Unencrypted().at(0).data));
}

TEST(ThresholdFilterFnTest, OnRecordRejectsWhenAlreadyFaulted) {
  FakeContext context;
  ThresholdFilterFn fn;

  RunContext run_context = CreateValidRunContext({{10, 1}}, 100, 200, 50);
  SignedRunContext signed_context = CreateValidSignedRunContext(run_context);
  auto report_payload = CreateThresholdReportPayload(
      kTestRunId, {{.project_id = 10, .metric_id = 1, .epoch_index = 150}}, signed_context);
  ASSERT_THAT(fn.Do(CreateVerificationKV(report_payload), context), IsOk());

  MetricEpochKey approved_key =
      CreateMetricEpochKey({.project_id = 10, .metric_id = 1, .epoch_index = 150});
  ShuffledMessage bad_msg = CreateShuffledMessage({
      .payload = "wrong_run_id",
      .run_id = "different_run_id_0123456789abcdef",
      .metric_epoch_key = approved_key,
      .shuffle_count = 0,
  });

  // Record 1 triggers the integrity fault.
  EXPECT_THAT(fn.Do(KV(bad_msg.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));

  // Record 2 is rejected because fault_handler_.HasFault() is true; fault is emitted at most once.
  ShuffledMessage valid_msg = CreateShuffledMessage({
      .payload = "valid_payload",
      .run_id = kTestRunId,
      .metric_epoch_key = approved_key,
      .shuffle_count = 0,
  });
  EXPECT_THAT(fn.Do(KV(valid_msg.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));
}

class TestThresholdFilterFn : public ThresholdFilterFn {
 public:
  using ThresholdFilterFn::OnConfig;
};

TEST(ThresholdFilterFnTest, OnConfigRejectsWhenAlreadyFaulted) {
  FakeContext context;
  TestThresholdFilterFn fn;

  // Trigger an integrity fault on the first config.
  auto fault_payload = CreateIntegrityFaultPayload();
  EXPECT_THAT(fn.OnConfig(fault_payload, context), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));

  // Verify that subsequent OnConfig call rejects because fault_handler_.HasFault() is true; fault
  // is emitted at most once.
  RunContext run_context = CreateValidRunContext({{10, 1}}, 100, 200, 50);
  SignedRunContext signed_context = CreateValidSignedRunContext(run_context);
  auto report_payload = CreateThresholdReportPayload(
      kTestRunId, {{.project_id = 10, .metric_id = 1, .epoch_index = 150}}, signed_context);

  EXPECT_THAT(fn.OnConfig(report_payload, context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));
}

TEST(ThresholdFilterFnTest, FinalizeSucceedsWhenDoIsNeverCalled) {
  FakeContext context;
  ThresholdFilterFn fn;

  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.Unencrypted(), IsEmpty());
}

TEST(ThresholdFilterFnTest, FinalizeFailsWhenConfigIsInvalid) {
  FakeContext context;
  ThresholdFilterFn fn;

  EXPECT_THAT(fn.Do(KV(google::protobuf::Any(), ""), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
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
