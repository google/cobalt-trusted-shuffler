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

#include <cstdint>

#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "cc/crypto/signing_key.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/crypto/verify_signed_run_context.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/integration/pipeline_runner.h"
#include "src/testing/integration/records.h"
#include "src/testing/test_utils.h"

namespace cobalt::trusted_shuffler::testing::integration {
namespace {

using ::absl_testing::IsOk;
using ::testing::IsEmpty;
using ::testing::SizeIs;

// Deterministic signing key implementation for testing.
class TestSigningKey : public oak::crypto::SigningKeyHandle {
 public:
  absl::StatusOr<oak::crypto::v1::Signature> Sign(absl::string_view /*message*/) override {
    oak::crypto::v1::Signature sig;
    sig.set_signature("test_signature_bytes");
    return sig;
  }
};

RunConfig CreateTestRunConfig(absl::Span<const MetricIdentifierSpec> metrics,
                              const int64_t min_epoch, const int64_t max_epoch,
                              const int32_t batch_size) {
  RunConfig config;
  for (const MetricIdentifierSpec& spec : metrics) {
    RunConfig::MetricIdentifier* const id = config.add_metric_identifiers();
    id->set_project_id(spec.project_id);
    id->set_metric_id(spec.metric_id);
  }
  config.set_min_epoch_index(min_epoch);
  config.set_max_epoch_index(max_epoch);
  config.set_target_batch_size(batch_size);
  return config;
}

TEST(ShufflerFlowIntegrationTest, RunContextInitializerEmitsSignedRunContext) {
  const int32_t project_id = 1001;
  const int32_t metric_id_above_threshold = 2;
  const int32_t metric_id_below_threshold = 3;
  const int64_t min_epoch_index = 0;
  const int64_t max_epoch_index = 10;
  const int32_t target_batch_size = 5;

  TestSigningKey signing_key;
  const RunConfig run_config = CreateTestRunConfig(
      {
          {.project_id = project_id, .metric_id = metric_id_above_threshold},
          {.project_id = project_id, .metric_id = metric_id_below_threshold},
      },
      min_epoch_index, max_epoch_index, target_batch_size);

  absl::StatusOr<PipelineExecutionResult> result = ExecutePipeline(signing_key, run_config);
  ASSERT_THAT(result.status(), IsOk());

  EXPECT_THAT(result->run_context_initializer_output.unencrypted, IsEmpty());
  ASSERT_THAT(result->run_context_initializer_output.encrypted, SizeIs(1));

  SignedRunContext signed_run_context;
  ASSERT_TRUE(signed_run_context.ParseFromString(
      result->run_context_initializer_output.encrypted[0].ciphertext()));
  EXPECT_EQ(signed_run_context.signature(), "test_signature_bytes");

  absl::StatusOr<RunContext> verified_context = VerifySignedRunContext(signed_run_context);
  ASSERT_THAT(verified_context.status(), IsOk());
  EXPECT_EQ(verified_context->run_config().min_epoch_index(), min_epoch_index);
  EXPECT_EQ(verified_context->run_config().max_epoch_index(), max_epoch_index);
  EXPECT_EQ(verified_context->run_config().target_batch_size(), target_batch_size);
}

}  // namespace
}  // namespace cobalt::trusted_shuffler::testing::integration
