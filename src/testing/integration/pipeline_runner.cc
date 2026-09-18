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

#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "cc/crypto/signing_key.h"
#include "src/containers/ingest_uploads/ingest_uploads_fn.h"
#include "src/containers/run_context_initializer/run_context_initializer_fn.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/integration/do_fn_runner.h"
#include "src/testing/integration/ingest_uploads_router.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

namespace {

constexpr int kIngestMessageKeyIndex = 0;
constexpr int kIngestReportKeyIndex = 1;

}  // namespace

absl::StatusOr<PipelineExecutionResult> ExecutePipeline(
    const PipelineRouters& routers, oak::crypto::SigningKeyHandle& signing_key,
    const RunConfig& run_config, absl::Span<const std::string> raw_checkpoints) {
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
  const EncryptedBlob& signed_run_context = result.run_context_initializer_output.encrypted[0];

  // 2. IngestUploads
  // Replica requirement: Can use any number of replicas. Client upload
  // checkpoints can be partitioned across arbitrary parallel replicas, with
  // each replica emitting partition messages and one PartialIngestionReport for
  // its assigned shard.
  // Output: key plus payload. The host reads the IngestMessagesHeader to tell
  // partition messages from partial ingestion reports.
  absl::StatusOr<std::vector<KVCollection>> ingest_shards =
      routers.ingest_uploads_router(signed_run_context, raw_checkpoints);
  if (!ingest_shards.ok()) {
    return ingest_shards.status();
  }

  for (const KVCollection& shard_inputs : *ingest_shards) {
    IngestUploadsFn ingest_fn(/*message_key_index=*/kIngestMessageKeyIndex,
                              /*report_key_index=*/kIngestReportKeyIndex);
    absl::StatusOr<KVBlobStageOutputs> ingest_out =
        DoFnRunner::RunReplicaWithKVBlobOutput(ingest_fn, {.inputs = shard_inputs});
    if (!ingest_out.ok()) {
      return ingest_out.status();
    }
    result.ingest_uploads_output.Append(*std::move(ingest_out));
  }

  // Validate that IngestUploads outputs can be split cleanly into partial reports and messages.
  absl::StatusOr<IngestUploadsOutputs> split_outputs =
      routers.ingest_uploads_output_splitter(result.ingest_uploads_output);
  if (!split_outputs.ok()) {
    return split_outputs.status();
  }

  return result;
}

}  // namespace cobalt::trusted_shuffler::testing::integration
