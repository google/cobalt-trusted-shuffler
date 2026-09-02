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

#include "src/containers/filter_and_strip_privacy_id/filter_and_strip_privacy_id_transform.h"

#include <memory>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/containers/filter_and_strip_privacy_id/filter_and_strip_privacy_id_fn.h"
#include "src/testing/fake_keys.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;

TEST(FilterAndStripPrivacyIdTransformTest, CreateTransformReturnsNonNull) {
  std::unique_ptr<confidential_federated_compute::fns::FnConfidentialTransform> transform =
      CreateFilterAndStripPrivacyIdTransform(std::make_unique<testing::FakeSigningKey>(),
                                             std::make_unique<testing::FakeEncryptionKeyHandle>());

  EXPECT_NE(transform, nullptr);
}

TEST(FilterAndStripPrivacyIdTransformTest, FactoryReturnsFilterAndStripPrivacyIdFn) {
  FilterAndStripPrivacyIdFnFactory factory;
  absl::StatusOr<std::unique_ptr<confidential_federated_compute::fns::Fn>> fn = factory.CreateFn();
  ASSERT_THAT(fn, IsOk());

  FilterAndStripPrivacyIdFn* filter_fn = dynamic_cast<FilterAndStripPrivacyIdFn*>(fn.value().get());
  ASSERT_NE(filter_fn, nullptr);
}

TEST(FilterAndStripPrivacyIdTransformTest, FactoryRetainsConfigConstraints) {
  TrustedShufflerConfigConstraints constraints;
  auto* metric_threshold = constraints.add_metric_thresholds();
  metric_threshold->set_project_id(1001);
  metric_threshold->set_metric_id(2);
  metric_threshold->set_threshold(100);

  FilterAndStripPrivacyIdFnFactory factory(constraints);
  EXPECT_EQ(factory.ConfigConstraints().metric_thresholds_size(), 1);
  EXPECT_EQ(factory.ConfigConstraints().metric_thresholds(0).project_id(), 1001);

  absl::StatusOr<std::unique_ptr<confidential_federated_compute::fns::Fn>> fn = factory.CreateFn();
  ASSERT_THAT(fn, IsOk());
  auto* filter_fn = dynamic_cast<FilterAndStripPrivacyIdFn*>(fn.value().get());
  ASSERT_NE(filter_fn, nullptr);
  EXPECT_EQ(filter_fn->ConfigConstraints().metric_thresholds_size(), 1);
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
