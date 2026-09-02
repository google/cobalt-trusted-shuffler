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

#include "src/containers/filter_and_strip_privacy_id/filter_and_strip_privacy_id_fn.h"

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/proto/config_constraints.pb.h"
#include "src/testing/fake_context.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::testing::IsEmpty;

TEST(FilterAndStripPrivacyIdFnTest, DoDoesNothing) {
  testing::FakeContext context;
  FilterAndStripPrivacyIdFn fn;

  EXPECT_THAT(fn.Do(FilterAndStripPrivacyIdFn::KV(), context), IsOk());

  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.Unencrypted(), IsEmpty());
}

TEST(FilterAndStripPrivacyIdFnTest, ConfigConstraintsAccessor) {
  TrustedShufflerConfigConstraints constraints;
  auto* metric_threshold = constraints.add_metric_thresholds();
  metric_threshold->set_project_id(1001);
  metric_threshold->set_metric_id(2);
  metric_threshold->set_threshold(100);

  FilterAndStripPrivacyIdFn fn(constraints);
  EXPECT_EQ(fn.ConfigConstraints().metric_thresholds_size(), 1);
  EXPECT_EQ(fn.ConfigConstraints().metric_thresholds(0).project_id(), 1001);
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
