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

#include "src/containers/gather_shuffle/gather_shuffle_transform.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "containers/fns/confidential_transform_server.h"
#include "containers/fns/do_fn.h"
#include "containers/fns/fn.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "gtest/gtest.h"
#include "src/containers/gather_shuffle/gather_shuffle_fn.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/fake_context.h"
#include "src/testing/fake_keys.h"
#include "src/testing/test_utils.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cobalt::trusted_shuffler::testing::CreateIngestionVerificationPayload;
using ::cobalt::trusted_shuffler::testing::CreateMetricEpochKey;
using ::cobalt::trusted_shuffler::testing::CreateValidSignedRunContext;
using ::cobalt::trusted_shuffler::testing::FakeCombinedContext;
using ::cobalt::trusted_shuffler::testing::FakeEncryptionKeyHandle;
using ::cobalt::trusted_shuffler::testing::FakeSigningKey;
using ::confidential_federated_compute::fns::DoFn;
using ::confidential_federated_compute::fns::Fn;
using ::confidential_federated_compute::fns::FnConfidentialTransform;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;

TEST(GatherShuffleTransformTest, CreateGatherShuffleTransformReturnsNonNull) {
  const std::unique_ptr<FnConfidentialTransform> transform = CreateGatherShuffleTransform(
      std::make_unique<FakeSigningKey>(), std::make_unique<FakeEncryptionKeyHandle>());

  EXPECT_NE(transform, nullptr);
}

TEST(GatherShuffleTransformTest, GatherShuffleFnFactoryReturnsGatherShuffleFn) {
  const GatherShuffleFnFactory factory;
  const absl::StatusOr<std::unique_ptr<Fn>> fn = factory.CreateFn();
  ASSERT_THAT(fn, IsOk());

  const GatherShuffleFn* const gather_shuffle_fn = dynamic_cast<GatherShuffleFn*>(fn.value().get());
  ASSERT_NE(gather_shuffle_fn, nullptr);
}

TEST(GatherShuffleTransformTest, FnFactoryCreatedFnExecutesLifecycleAndEmitsShuffledGroup) {
  const GatherShuffleFnFactory factory;
  absl::StatusOr<std::unique_ptr<Fn>> fn_or = factory.CreateFn();
  ASSERT_THAT(fn_or, IsOk());
  std::unique_ptr<Fn> fn = std::move(*fn_or);

  FakeCombinedContext context;
  ASSERT_THAT(fn->InitializeReplica(google::protobuf::Any(), context), IsOk());

  const MetricEpochKey partition =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 2, .epoch_index = 50});
  const SignedRunContext signed_run_context =
      CreateValidSignedRunContext(1001, 2, testing::kTestRunId, 0, 100);
  // With 150 messages and target_batch_size = 100:
  // batch_num = 2, actual_batch_size = 75 -> gather group 0 expects 2 * (37 + 1) = 76 messages.
  const IngestionVerificationPayload config_payload = CreateIngestionVerificationPayload(
      testing::kTestRunId, /*approved_partitions=*/{partition},
      /*canonical_fingerprint=*/{{partition, 150}}, signed_run_context);

  auto* const do_fn = dynamic_cast<DoFn*>(fn.get());
  ASSERT_NE(do_fn, nullptr);
  ASSERT_THAT(do_fn->Do(DoFn::KV(config_payload.SerializeAsString()), context), IsOk());

  const int expected_group_size = 76;
  for (int i = 0; i < expected_group_size; ++i) {
    ShuffledMessage msg;
    *msg.mutable_metric_epoch_key() = partition;
    msg.set_run_id(testing::kTestRunId);
    msg.set_shuffle_count(1);
    msg.set_group_id(0);
    ASSERT_THAT(do_fn->Do(DoFn::KV(msg.SerializeAsString()), context), IsOk());
  }

  ASSERT_THAT(fn->FinalizeReplica(google::protobuf::Any(), context), IsOk());
  ASSERT_THAT(context.Encrypted(), SizeIs(expected_group_size));

  for (const auto& output : context.Encrypted()) {
    EXPECT_THAT(output.first, Eq(0));
    ShuffledMessage result;
    ASSERT_TRUE(result.ParseFromString(output.second.data));
    EXPECT_THAT(result.shuffle_count(), Eq(2));
    EXPECT_THAT(result.group_id(), Eq(0));
  }
}

TEST(GatherShuffleTransformTest, FnFactoryCreatedFnFinalizeReplicaSucceedsWhenDoIsNeverCalled) {
  const GatherShuffleFnFactory factory;
  absl::StatusOr<std::unique_ptr<Fn>> fn_or = factory.CreateFn();
  ASSERT_THAT(fn_or, IsOk());
  std::unique_ptr<Fn> fn = std::move(*fn_or);

  FakeCombinedContext context;
  ASSERT_THAT(fn->InitializeReplica(google::protobuf::Any(), context), IsOk());
  EXPECT_THAT(fn->FinalizeReplica(google::protobuf::Any(), context), IsOk());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.Unencrypted(), IsEmpty());
}

TEST(GatherShuffleTransformTest, FnFactoryCreatedFnEmitsIntegrityFaultWhenConfigIsInvalid) {
  const GatherShuffleFnFactory factory;
  absl::StatusOr<std::unique_ptr<Fn>> fn_or = factory.CreateFn();
  ASSERT_THAT(fn_or, IsOk());
  std::unique_ptr<Fn> fn = std::move(*fn_or);
  auto* const do_fn = dynamic_cast<DoFn*>(fn.get());
  ASSERT_NE(do_fn, nullptr);

  FakeCombinedContext context;
  ASSERT_THAT(fn->InitializeReplica(google::protobuf::Any(), context), IsOk());
  EXPECT_THAT(do_fn->Do(DoFn::KV(""), context), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(fn->FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  EXPECT_THAT(context.Encrypted(), IsEmpty());
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  IntegrityFault fault;
  EXPECT_TRUE(fault.ParseFromString(context.Unencrypted()[0].data));
}

TEST(GatherShuffleTransformTest, FnFactoryCreatedFnEmitsIntegrityFaultOnInvalidRecord) {
  const GatherShuffleFnFactory factory;
  absl::StatusOr<std::unique_ptr<Fn>> fn_or = factory.CreateFn();
  ASSERT_THAT(fn_or, IsOk());
  std::unique_ptr<Fn> fn = std::move(*fn_or);

  FakeCombinedContext context;
  ASSERT_THAT(fn->InitializeReplica(google::protobuf::Any(), context), IsOk());

  const MetricEpochKey partition =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 2, .epoch_index = 50});
  const SignedRunContext signed_run_context =
      CreateValidSignedRunContext(1001, 2, testing::kTestRunId, 0, 100);
  const IngestionVerificationPayload config_payload = CreateIngestionVerificationPayload(
      testing::kTestRunId, /*approved_partitions=*/{partition},
      /*canonical_fingerprint=*/{{partition, 100}}, signed_run_context);

  auto* const do_fn = dynamic_cast<DoFn*>(fn.get());
  ASSERT_NE(do_fn, nullptr);
  ASSERT_THAT(do_fn->Do(DoFn::KV(config_payload.SerializeAsString()), context), IsOk());

  ShuffledMessage unshuffled_msg;
  *unshuffled_msg.mutable_metric_epoch_key() = partition;
  unshuffled_msg.set_run_id(testing::kTestRunId);
  unshuffled_msg.set_shuffle_count(0);
  unshuffled_msg.set_group_id(0);
  EXPECT_THAT(do_fn->Do(DoFn::KV(unshuffled_msg.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  EXPECT_THAT(context.Encrypted(), IsEmpty());
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  IntegrityFault fault;
  EXPECT_TRUE(fault.ParseFromString(context.Unencrypted()[0].data));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
