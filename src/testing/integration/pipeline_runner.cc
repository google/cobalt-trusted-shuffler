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
#include "src/containers/ingestion_verification/ingestion_verification_fn.h"
#include "src/containers/run_context_initializer/run_context_initializer_fn.h"
#include "src/proto/config_constraints.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/integration/do_fn_runner.h"
#include "src/testing/integration/ingest_uploads_router.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

absl::StatusOr<PipelineExecutionResult> ExecutePipeline(
    const PipelineRouters& routers, oak::crypto::SigningKeyHandle& signing_key,
    const RunConfig& run_config, const TrustedShufflerConfigConstraints& constraints,
    absl::Span<const std::string> raw_checkpoints) {
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
    IngestUploadsFn ingest_fn;
    absl::StatusOr<KVBlobStageOutputs> ingest_out =
        DoFnRunner::RunReplicaWithKVBlobOutput(ingest_fn, {.inputs = shard_inputs});
    if (!ingest_out.ok()) {
      return ingest_out.status();
    }
    result.ingest_uploads_output.Append(*std::move(ingest_out));
  }

  // Separate IngestUploads outputs into PartialIngestionReports and partition messages.
  absl::StatusOr<IngestUploadsOutputs> split_outputs =
      routers.ingest_uploads_output_splitter(result.ingest_uploads_output);
  if (!split_outputs.ok()) {
    return split_outputs.status();
  }

  // 3. IngestionVerification
  // Replica requirement: Requires a single replica to process all partial
  // reports. Must aggregate the complete set of PartialIngestionReports from
  // every IngestUploads replica to evaluate global unique-device privacy
  // thresholds and compute the total input multiset fingerprint.
  // Output: key plus payload. IngestionVerificationFn packs the ingestion
  // result marker or integrity fault into the key for the host to act on.
  absl::StatusOr<KVCollection> iv_inputs =
      routers.ingestion_verification_router(signed_run_context, split_outputs->partial_reports);
  if (!iv_inputs.ok()) {
    return iv_inputs.status();
  }

  IngestionVerificationFn iv_fn(/*constraints=*/constraints);
  absl::StatusOr<KVBlobStageOutputs> iv_out =
      DoFnRunner::RunReplicaWithKVBlobOutput(iv_fn, {.inputs = *iv_inputs});
  if (!iv_out.ok()) {
    return iv_out.status();
  }
  result.ingestion_verification_output = *std::move(iv_out);

  if (result.ingestion_verification_output.encrypted.size() != 1) {
    return absl::InternalError(
        absl::StrCat("Expected IngestionVerificationFn to emit exactly 1 encrypted "
                     "IngestionVerificationPayload, but got ",
                     result.ingestion_verification_output.encrypted.size(), "."));
  }

  IngestionVerificationHeader iv_header;
  if (!result.ingestion_verification_output.encrypted[0].key().UnpackTo(&iv_header)) {
    return absl::InternalError(
        "Failed to unpack IngestionVerificationHeader from IngestionVerification output key.");
  }
  if (iv_header.has_integrity_fault()) {
    return absl::FailedPreconditionError(
        "IngestionVerification reported an integrity fault in IngestionVerificationHeader.");
  }
  if (!iv_header.has_ingestion_result()) {
    return absl::InternalError(
        "IngestionVerificationHeader has neither ingestion_result nor integrity_fault.");
  }

  return result;
}

}  // namespace cobalt::trusted_shuffler::testing::integration
