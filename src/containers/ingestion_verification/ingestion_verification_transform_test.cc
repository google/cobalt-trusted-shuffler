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

#include "src/containers/ingestion_verification/ingestion_verification_transform.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "containers/fns/confidential_transform_server.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "gtest/gtest.h"
#include "src/containers/ingestion_verification/ingestion_verification_fn.h"
#include "src/proto/common.pb.h"
#include "src/proto/config_constraints.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/sketch/hll_sketch.h"
#include "src/testing/fake_context.h"
#include "src/testing/fake_keys.h"
#include "src/testing/matchers.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cobalt::trusted_shuffler::testing::EquivToProto;
using ::confidential_federated_compute::fns::Fn;
using ::confidential_federated_compute::fns::FnConfidentialTransform;
using ::confidential_federated_compute::fns::FnFactory;
using ::confidential_federated_compute::fns::WriteConfigurationMap;
using ::testing::ElementsAre;

struct MetricThresholdSpec {
  int32_t project_id;
  int32_t metric_id;
  int32_t threshold;
};

TrustedShufflerConfigConstraints CreateConfigConstraints(
    absl::Span<const MetricThresholdSpec> specs) {
  TrustedShufflerConfigConstraints constraints;
  for (const auto& spec : specs) {
    auto* const mt = constraints.add_metric_thresholds();
    mt->set_project_id(spec.project_id);
    mt->set_metric_id(spec.metric_id);
    mt->set_threshold(spec.threshold);
  }
  return constraints;
}

absl::StatusOr<std::unique_ptr<FnFactory>> ProvideFnFactory(
    const TrustedShufflerConfigConstraints& constraints) {
  google::protobuf::Any constraints_any;
  constraints_any.PackFrom(constraints);
  return ProvideIngestionVerificationFnFactory(google::protobuf::Any(), constraints_any,
                                               WriteConfigurationMap{});
}

absl::StatusOr<std::unique_ptr<FnFactory>> ProvideFnFactory(
    absl::Span<const MetricThresholdSpec> specs) {
  return ProvideFnFactory(CreateConfigConstraints(specs));
}

TEST(IngestionVerificationTransformTest, CreateIngestionVerificationTransformReturnsNonNull) {
  const std::unique_ptr<FnConfidentialTransform> transform =
      CreateIngestionVerificationTransform(std::make_unique<testing::FakeSigningKey>(),
                                           std::make_unique<testing::FakeEncryptionKeyHandle>());

  EXPECT_NE(transform, nullptr);
}

TEST(IngestionVerificationTransformTest, IngestionVerificationFnFactoryCreatesFnWithConstraints) {
  const auto constraints =
      CreateConfigConstraints({{.project_id = 1001, .metric_id = 2, .threshold = 50}});

  IngestionVerificationFnFactory factory(constraints);
  EXPECT_THAT(factory.ConfigConstraints(), EquivToProto(constraints));

  const auto fn = factory.CreateFn();
  ASSERT_THAT(fn, IsOk());

  auto* const iv_fn = dynamic_cast<IngestionVerificationFn*>(fn.value().get());
  ASSERT_NE(iv_fn, nullptr);
  EXPECT_THAT(iv_fn->ConfigConstraints(), EquivToProto(constraints));
}

TEST(IngestionVerificationTransformTest,
     ProvideIngestionVerificationFnFactorySucceedsWithValidConstraints) {
  const auto expected =
      CreateConfigConstraints({{.project_id = 1, .metric_id = 10, .threshold = 50},
                               {.project_id = 1, .metric_id = 20, .threshold = 100}});

  const auto factory_or = ProvideFnFactory(expected);
  ASSERT_THAT(factory_or, IsOk());
  EXPECT_NE(factory_or.value(), nullptr);
  auto* const factory = dynamic_cast<IngestionVerificationFnFactory*>(factory_or->get());
  ASSERT_NE(factory, nullptr);
  EXPECT_THAT(factory->ConfigConstraints(), EquivToProto(expected));
}

TEST(IngestionVerificationTransformTest, ProvideIngestionVerificationFnFactoryFailsWithInvalidAny) {
  EXPECT_THAT(ProvideIngestionVerificationFnFactory(
                  google::protobuf::Any(), google::protobuf::Any(), WriteConfigurationMap{}),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(IngestionVerificationTransformTest,
     ProvideIngestionVerificationFnFactoryFailsWithNonPositiveProjectId) {
  EXPECT_THAT(ProvideFnFactory({{.project_id = 0, .metric_id = 10, .threshold = 50}}),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(IngestionVerificationTransformTest,
     ProvideIngestionVerificationFnFactoryFailsWithNonPositiveMetricId) {
  EXPECT_THAT(ProvideFnFactory({{.project_id = 1, .metric_id = 0, .threshold = 50}}),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(IngestionVerificationTransformTest,
     ProvideIngestionVerificationFnFactoryFailsWithNonPositiveThreshold) {
  EXPECT_THAT(ProvideFnFactory({{.project_id = 1, .metric_id = 10, .threshold = 0}}),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(IngestionVerificationTransformTest,
     ProvideIngestionVerificationFnFactoryFailsWithDuplicateMetricThreshold) {
  EXPECT_THAT(ProvideFnFactory({{.project_id = 1, .metric_id = 10, .threshold = 50},
                                {.project_id = 1, .metric_id = 10, .threshold = 100}}),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(IngestionVerificationTransformTest,
     ProvideIngestionVerificationFnFactoryCreatesFnWithConstraints) {
  const auto expected =
      CreateConfigConstraints({{.project_id = 1001, .metric_id = 1, .threshold = 100}});

  const auto factory_or = ProvideFnFactory(expected);
  ASSERT_THAT(factory_or, IsOk());

  const auto fn = (*factory_or)->CreateFn();
  ASSERT_THAT(fn, IsOk());

  auto* const iv_fn = dynamic_cast<IngestionVerificationFn*>(fn.value().get());
  ASSERT_NE(iv_fn, nullptr);
  EXPECT_THAT(iv_fn->ConfigConstraints(), EquivToProto(expected));
}

TEST(IngestionVerificationTransformTest, FnFactoryCreatedFnExecutesLifecycleAndEmitsOutput) {
  const auto factory_or = ProvideFnFactory({{.project_id = 1001, .metric_id = 2, .threshold = 10}});
  ASSERT_THAT(factory_or, IsOk());

  auto fn_or = (*factory_or)->CreateFn();
  ASSERT_THAT(fn_or, IsOk());
  std::unique_ptr<Fn> fn = std::move(fn_or.value());

  testing::FakeContext context;
  ASSERT_THAT(fn->InitializeReplica(google::protobuf::Any(), context), IsOk());

  // Record 0: SignedRunContext
  RunContext run_context;
  run_context.set_run_id("run-transform-test");
  run_context.set_run_hash_key("hash-key-transform-test");
  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature("valid-sig");

  auto* const do_fn = dynamic_cast<confidential_federated_compute::fns::DoFn*>(fn.get());
  ASSERT_NE(do_fn, nullptr);
  ASSERT_THAT(
      do_fn->Do(confidential_federated_compute::fns::DoFn::KV(signed_context.SerializeAsString()),
                context),
      IsOk());

  // Record 1: PartialIngestionReport
  PartialIngestionReport report;
  report.mutable_partial_fingerprint()->set_run_id("run-transform-test");
  report.mutable_partial_fingerprint()->set_stage_id(Stage::STAGE_INGESTION);
  report.mutable_partial_fingerprint()->set_is_partial(true);
  FingerprintEntry* const entry = report.mutable_partial_fingerprint()->add_entries();
  entry->mutable_metric_epoch_key()->set_project_id(1001);
  entry->mutable_metric_epoch_key()->set_metric_id(2);
  entry->mutable_metric_epoch_key()->set_epoch_index(100);
  entry->set_message_count(15);

  MetricEpochSketch* const sketch_entry = report.add_metric_epoch_sketches();
  sketch_entry->mutable_metric_epoch_key()->set_project_id(1001);
  sketch_entry->mutable_metric_epoch_key()->set_metric_id(2);
  sketch_entry->mutable_metric_epoch_key()->set_epoch_index(100);

  auto hll = HllSketch::Create(14).value();
  for (int i = 0; i < 15; ++i) {
    hll.Insert(absl::StrCat("device_", i));
  }
  *sketch_entry->mutable_hyperloglog_sketch() = hll.ToProto();

  ASSERT_THAT(
      do_fn->Do(confidential_federated_compute::fns::DoFn::KV(report.SerializeAsString()), context),
      IsOk());
  ASSERT_THAT(fn->FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_EQ(context.Encrypted().size(), 1);
  EXPECT_EQ(context.Encrypted()[0].first, IngestionVerificationFn::kReencryptionKeyIndex);

  IngestionVerificationHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_FALSE(header.has_integrity_fault());
  ASSERT_EQ(header.summary_report().partition_counts_size(), 1);

  IngestionVerificationPayload payload;
  ASSERT_TRUE(payload.ParseFromString(context.Encrypted()[0].second.data));
  ASSERT_TRUE(payload.has_success());
  EXPECT_EQ(payload.success().threshold_report().approved_partitions_size(), 1);
  EXPECT_EQ(payload.success().canonical_fingerprint().entries_size(), 1);
  EXPECT_THAT(payload.success().signed_run_context(), EquivToProto(signed_context));
}

TEST(IngestionVerificationTransformTest,
     FnFactoryCreatedFnEmitsIntegrityFaultWhenNoConfigProvided) {
  const auto factory_or = ProvideFnFactory({{.project_id = 1001, .metric_id = 2, .threshold = 10}});
  ASSERT_THAT(factory_or, IsOk());

  auto fn_or = (*factory_or)->CreateFn();
  ASSERT_THAT(fn_or, IsOk());
  std::unique_ptr<Fn> fn = std::move(fn_or.value());

  testing::FakeContext context;
  ASSERT_THAT(fn->InitializeReplica(google::protobuf::Any(), context), IsOk());
  ASSERT_THAT(fn->FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_EQ(context.Encrypted().size(), 1);
  EXPECT_EQ(context.Encrypted()[0].first, IngestionVerificationFn::kReencryptionKeyIndex);

  IngestionVerificationHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_TRUE(header.has_integrity_fault());

  IngestionVerificationPayload payload;
  ASSERT_TRUE(payload.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_TRUE(payload.has_integrity_fault());
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
