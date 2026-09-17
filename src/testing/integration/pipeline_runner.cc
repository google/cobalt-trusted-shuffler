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

#include "src/testing/integration/pipeline_runner.h"

#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "cc/crypto/signing_key.h"
#include "src/containers/run_context_initializer/run_context_initializer_fn.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/integration/do_fn_runner.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

absl::StatusOr<PipelineExecutionResult> ExecutePipeline(oak::crypto::SigningKeyHandle& signing_key,
                                                        const RunConfig& run_config) {
  PipelineExecutionResult result;

  // 1. RunContextInitializer
  // Replica requirement: Requires a single replica per pipeline run. Generates
  // unique per-run random cryptographic keys (run_hash_key) and run_id that
  // must be shared across all downstream stages in the run.
  // Output: payload only. Downstream stages take the SignedRunContext as a
  // shared configuration record and have no key to route on.
  RunContextInitializerFn rci_fn(/*signing_key_handle=*/&signing_key);
  absl::StatusOr<BlobStageOutputs> rci_output =
      DoFnRunner::RunReplicaWithBlobOutput(rci_fn, {.init_config = run_config});
  if (!rci_output.ok()) {
    return rci_output.status();
  }
  result.run_context_initializer_output = *std::move(rci_output);

  if (result.run_context_initializer_output.encrypted.size() != 1) {
    return absl::InternalError(
        absl::StrCat("Expected RunContextInitializerFn to emit exactly 1 encrypted "
                     "SignedRunContext, but got ",
                     result.run_context_initializer_output.encrypted.size(), "."));
  }

  return result;
}

}  // namespace cobalt::trusted_shuffler::testing::integration
