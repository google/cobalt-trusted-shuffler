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
#include "containers/fns/confidential_transform_server.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "gtest/gtest.h"
#include "src/containers/ingestion_verification/ingestion_verification_fn.h"
#include "src/proto/config_constraints.pb.h"
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
    const std::vector<MetricThresholdSpec>& specs) {
  TrustedShufflerConfigConstraints constraints;
  for (const auto& spec : specs) {
    auto* mt = constraints.add_metric_thresholds();
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
    const std::vector<MetricThresholdSpec>& specs) {
  return ProvideFnFactory(CreateConfigConstraints(specs));
}

TEST(IngestionVerificationTransformTest, CreateIngestionVerificationTransformReturnsNonNull) {
  std::unique_ptr<FnConfidentialTransform> transform =
      CreateIngestionVerificationTransform(std::make_unique<testing::FakeSigningKey>(),
                                           std::make_unique<testing::FakeEncryptionKeyHandle>());

  EXPECT_NE(transform, nullptr);
}

TEST(IngestionVerificationTransformTest, IngestionVerificationFnFactoryCreatesFnWithConstraints) {
  TrustedShufflerConfigConstraints constraints =
      CreateConfigConstraints({{.project_id = 1001, .metric_id = 2, .threshold = 50}});

  IngestionVerificationFnFactory factory(constraints);
  EXPECT_THAT(factory.ConfigConstraints(), EquivToProto(constraints));

  absl::StatusOr<std::unique_ptr<Fn>> fn = factory.CreateFn();
  ASSERT_THAT(fn, IsOk());

  IngestionVerificationFn* iv_fn = dynamic_cast<IngestionVerificationFn*>(fn.value().get());
  ASSERT_NE(iv_fn, nullptr);
  EXPECT_THAT(iv_fn->ConfigConstraints(), EquivToProto(constraints));
}

TEST(IngestionVerificationTransformTest,
     ProvideIngestionVerificationFnFactorySucceedsWithValidConstraints) {
  TrustedShufflerConfigConstraints expected =
      CreateConfigConstraints({{.project_id = 1, .metric_id = 10, .threshold = 50},
                               {.project_id = 1, .metric_id = 20, .threshold = 100}});

  auto factory_or = ProvideFnFactory(expected);
  ASSERT_THAT(factory_or, IsOk());
  EXPECT_NE(factory_or.value(), nullptr);
  auto* factory = dynamic_cast<IngestionVerificationFnFactory*>(factory_or->get());
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
  TrustedShufflerConfigConstraints expected =
      CreateConfigConstraints({{.project_id = 1001, .metric_id = 1, .threshold = 100}});

  auto factory_or = ProvideFnFactory(expected);
  ASSERT_THAT(factory_or, IsOk());

  absl::StatusOr<std::unique_ptr<Fn>> fn = (*factory_or)->CreateFn();
  ASSERT_THAT(fn, IsOk());

  IngestionVerificationFn* iv_fn = dynamic_cast<IngestionVerificationFn*>(fn.value().get());
  ASSERT_NE(iv_fn, nullptr);
  EXPECT_THAT(iv_fn->ConfigConstraints(), EquivToProto(expected));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
