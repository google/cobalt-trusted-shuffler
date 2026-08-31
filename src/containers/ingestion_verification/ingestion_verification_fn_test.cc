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

#include "src/containers/ingestion_verification/ingestion_verification_fn.h"

#include "absl/status/status_matchers.h"
#include "gtest/gtest.h"
#include "src/proto/config_constraints.pb.h"
#include "src/testing/fake_context.h"
#include "src/testing/matchers.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::cobalt::trusted_shuffler::testing::EquivToProto;
using ::cobalt::trusted_shuffler::testing::FakeContext;
using ::fcp::confidentialcompute::CommitRequest;
using ::fcp::confidentialcompute::ConfigureRequest;
using ::testing::ElementsAre;
using ::testing::IsEmpty;

TEST(IngestionVerificationFnTest, ConfigureReturnsOk) {
  FakeContext context;
  IngestionVerificationFn fn(TrustedShufflerConfigConstraints{});

  EXPECT_THAT(fn.Configure(ConfigureRequest(), context), IsOk());
}

TEST(IngestionVerificationFnTest, DoDoesNothing) {
  FakeContext context;
  IngestionVerificationFn fn(TrustedShufflerConfigConstraints{});

  // The container does nothing right now and emits no outputs.
  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(google::protobuf::Any(), "value"), context),
              IsOk());

  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.Releasable(), IsEmpty());
}

TEST(IngestionVerificationFnTest, ConfigConstraintsAccessor) {
  TrustedShufflerConfigConstraints constraints;
  auto* metric_threshold = constraints.add_metric_thresholds();
  metric_threshold->set_project_id(1001);
  metric_threshold->set_metric_id(2);
  metric_threshold->set_threshold(100);

  IngestionVerificationFn fn(constraints);
  EXPECT_THAT(fn.ConfigConstraints().metric_thresholds(),
              ElementsAre(EquivToProto(*metric_threshold)));
}

TEST(IngestionVerificationFnTest, CommitReturnsOk) {
  FakeContext context;
  IngestionVerificationFn fn(TrustedShufflerConfigConstraints{});

  EXPECT_THAT(fn.Commit(CommitRequest(), context), IsOk());
}

TEST(IngestionVerificationFnTest, FinalizeReplicaReturnsOk) {
  FakeContext context;
  IngestionVerificationFn fn(TrustedShufflerConfigConstraints{});

  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
