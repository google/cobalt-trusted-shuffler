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

#include <cstdint>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/proto/config_constraints.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/fake_context.h"
#include "src/testing/matchers.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cobalt::trusted_shuffler::testing::EquivToProto;
using ::cobalt::trusted_shuffler::testing::FakeContext;
using ::testing::ElementsAre;
using ::testing::Eq;

// Helper struct for declarative test fixtures without positional tuples.
struct MetricThresholdSpec {
  int32_t project_id;
  int32_t metric_id;
  int32_t threshold;
};

// Declarative helper requiring explicit arguments (no default parameters).
TrustedShufflerConfigConstraints CreateConfigConstraints(
    absl::Span<const MetricThresholdSpec> specs) {
  TrustedShufflerConfigConstraints constraints;
  for (const auto& spec : specs) {
    auto* mt = constraints.add_metric_thresholds();
    mt->set_project_id(spec.project_id);
    mt->set_metric_id(spec.metric_id);
    mt->set_threshold(spec.threshold);
  }
  return constraints;
}

SignedRunContext CreateValidSignedRunContext(int32_t project_id, int32_t metric_id,
                                             std::string signature_value) {
  RunContext run_context;
  run_context.set_run_id("test_run_id_32_bytes_abcdefghij");
  run_context.set_run_hash_key("test_hash_key_32_bytes_abcdefgh");

  RunConfig* config = run_context.mutable_run_config();
  config->set_min_epoch_index(10);
  config->set_max_epoch_index(20);
  config->set_target_batch_size(100);

  RunConfig::MetricIdentifier* metric = config->add_metric_identifiers();
  metric->set_project_id(project_id);
  metric->set_metric_id(metric_id);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::move(signature_value));
  return signed_context;
}

TEST(IngestionVerificationFnTest, SuccessfulConfigUnpackFollowedByDataRecords) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 50},
  };
  auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  SignedRunContext signed_context = CreateValidSignedRunContext(1001, 2, "valid_signature_bytes");

  // Record 0: Ingests configuration
  ASSERT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              IsOk());

  EXPECT_TRUE(fn.HasConfig());
  ASSERT_TRUE(fn.Config().has_value());
  EXPECT_THAT(fn.Config()->signature(), Eq("valid_signature_bytes"));

  // Records 1..N: Ingests client data records
  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV("client-record-1"), context), IsOk());
  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV("client-record-2"), context), IsOk());
}

TEST(IngestionVerificationFnTest, CorruptedOrInvalidSignedRunContextPayloadReturnsError) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 50},
  };
  auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  // Invalid proto wire data on record 0
  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV("corrupted_payload_string"), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());
}

TEST(IngestionVerificationFnTest, CorruptedInnerRunContextReturnsError) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 50},
  };
  auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  // Outer proto is SignedRunContext, but serialized_run_context is corrupt
  SignedRunContext signed_context;
  signed_context.set_serialized_run_context("\xff\xff\xff\xff");
  signed_context.set_signature("valid_signature");

  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());
}

TEST(IngestionVerificationFnTest, ConfigConstraintsAccessor) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 100},
  };
  auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  EXPECT_THAT(fn.ConfigConstraints().metric_thresholds(),
              ElementsAre(EquivToProto(constraints.metric_thresholds(0))));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
