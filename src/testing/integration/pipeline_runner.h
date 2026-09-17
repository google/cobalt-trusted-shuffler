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

#ifndef SRC_TESTING_INTEGRATION_PIPELINE_RUNNER_H_
#define SRC_TESTING_INTEGRATION_PIPELINE_RUNNER_H_

#include "absl/status/statusor.h"
#include "cc/crypto/signing_key.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

// Captures the host-visible outputs of every stage in a pipeline run. The type
// of each field states whether that stage keeps the routing key its records
// were emitted with.
struct PipelineExecutionResult {
  BlobStageOutputs run_context_initializer_output;
};

// Executes the implemented container stages end-to-end.
absl::StatusOr<PipelineExecutionResult> ExecutePipeline(oak::crypto::SigningKeyHandle& signing_key,
                                                        const RunConfig& run_config);

}  // namespace cobalt::trusted_shuffler::testing::integration

#endif  // SRC_TESTING_INTEGRATION_PIPELINE_RUNNER_H_
