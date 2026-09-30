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
#include "src/containers/encrypt_output/encrypt_output_fn.h"
#include "src/containers/gather_shuffle/gather_shuffle_fn.h"
#include "src/containers/ingest_uploads/ingest_uploads_fn.h"
#include "src/containers/ingestion_verification/ingestion_verification_fn.h"
#include "src/containers/key_releaser/key_releaser_fn.h"
#include "src/containers/pre_padding/pre_padding_fn.h"
#include "src/containers/run_context_initializer/run_context_initializer_fn.h"
#include "src/containers/scatter_shuffle/scatter_shuffle_fn.h"
#include "src/containers/threshold_filter/threshold_filter_fn.h"
#include "src/proto/config_constraints.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/fake_orchestrator.h"
#include "src/testing/integration/do_fn_runner.h"
#include "src/testing/integration/ingest_uploads_router.h"
#include "src/testing/integration/records.h"
#include "src/testing/test_utils.h"

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
  testing::FakeOrchestratorServer orchestrator(
      testing::GetDefaultTestSigningKey().EndorsedEvidence());
  RunContextInitializerFn rci_fn(/*signing_key_handle=*/&signing_key, orchestrator.Client());
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
  // each replica emitting ingested messages and one PartialIngestionReport for
  // its assigned shard.
  // Output: key plus payload. The host reads the IngestMessagesHeader to tell
  // ingested messages from partial ingestion reports.
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

  // Separate IngestUploads outputs into PartialIngestionReports and ingested messages.
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

  // The host has read the IngestionVerificationHeader and forwards only the
  // payload to the stages that take it as a shared configuration record.
  const EncryptedBlob verification_payload =
      result.ingestion_verification_output.encrypted[0].DropKey();

  // 4. ThresholdFilter
  // Replica requirement: Can use any number of replicas. Each replica receives
  // the shared IngestionVerificationPayload configuration record and independently
  // filters its assigned subset of ingested messages. Here we execute a single
  // replica over all ingested messages.
  // Output: key plus payload. ThresholdFilterFn forwards each surviving record
  // with its MetricEpochKey so the host can group by partition.
  absl::StatusOr<KVCollection> tf_inputs =
      routers.threshold_filter_router(verification_payload, split_outputs->ingested_messages);
  if (!tf_inputs.ok()) {
    return tf_inputs.status();
  }

  ThresholdFilterFn tf_fn;
  absl::StatusOr<KVBlobStageOutputs> tf_out =
      DoFnRunner::RunReplicaWithKVBlobOutput(tf_fn, {.inputs = *tf_inputs});
  if (!tf_out.ok()) {
    return tf_out.status();
  }
  result.threshold_filter_output = *std::move(tf_out);

  // 5. PrePadding
  // Replica requirement: Requires a single replica per pipeline run. It takes
  // the IngestionVerificationPayload to learn which partitions were approved, and
  // outputs all padding messages for all approved partitions.
  // Output: padding messages keyed by MetricEpochKey.
  absl::StatusOr<KVCollection> pp_inputs = routers.pre_padding_router(verification_payload);
  if (!pp_inputs.ok()) {
    return pp_inputs.status();
  }

  PrePaddingFn pp_fn;
  absl::StatusOr<KVBlobStageOutputs> pp_out =
      DoFnRunner::RunReplicaWithKVBlobOutput(pp_fn, {.inputs = *pp_inputs});
  if (!pp_out.ok()) {
    return pp_out.status();
  }
  result.pre_padding_output = *std::move(pp_out);

  // 6. ScatterShuffle
  // Replica requirement: Requires one replica per batch (can use any number of replicas across the
  // stage as long as each batch of target_batch_size messages is processed by its own unique
  // replica). Output: encrypted payloads with ScatterShuffleHeader keyed by ScatterGroupKey.
  absl::StatusOr<std::vector<KVCollection>> ss_inputs = routers.scatter_shuffle_router(
      verification_payload, run_config.target_batch_size(),
      result.threshold_filter_output.encrypted, result.pre_padding_output.encrypted);
  if (!ss_inputs.ok()) {
    return ss_inputs.status();
  }

  for (const KVCollection& batch_inputs : *ss_inputs) {
    ScatterShuffleFn ss_fn;
    absl::StatusOr<KVBlobStageOutputs> ss_out =
        DoFnRunner::RunReplicaWithKVBlobOutput(ss_fn, {.inputs = batch_inputs});
    if (!ss_out.ok()) {
      return ss_out.status();
    }
    result.scatter_shuffle_output.Append(*std::move(ss_out));
  }

  // 7. GatherShuffle
  // Replica requirement: Requires one replica per (MetricEpochKey, group_id)
  // gather group (can use any number of replicas across the stage as long as
  // each gather group is processed by its own unique replica).
  // Output: payload only. Downstream EncryptOutputFn reads MetricEpochKey from
  // the decrypted ShuffledMessage.
  absl::StatusOr<std::vector<KVCollection>> gather_shards =
      routers.gather_shuffle_router(verification_payload, result.scatter_shuffle_output.encrypted);
  if (!gather_shards.ok()) {
    return gather_shards.status();
  }

  for (const KVCollection& shard : *gather_shards) {
    GatherShuffleFn gather_fn;
    absl::StatusOr<BlobStageOutputs> gather_output =
        DoFnRunner::RunReplicaWithBlobOutput(gather_fn, {.inputs = shard});
    if (!gather_output.ok()) {
      return gather_output.status();
    }
    result.gather_shuffle_output.Append(*std::move(gather_output));
  }

  // 8. EncryptOutput
  // Replica requirement: Can use any number of replicas. Gathered two-pass
  // shuffled messages can be partitioned across arbitrary parallel replicas,
  // with each replica emitting unencrypted EncryptedMessageOutput records and
  // one encrypted PartialReleaseReport for its assigned shard.
  // Output: key plus payload. The host reads ReleaseOutputHeader to separate
  // EncryptedMessageOutput records from PartialReleaseReports.
  absl::StatusOr<std::vector<KVCollection>> encrypt_shards = routers.encrypt_output_router(
      verification_payload, result.gather_shuffle_output.encrypted);
  if (!encrypt_shards.ok()) {
    return encrypt_shards.status();
  }

  for (const KVCollection& shard_inputs : *encrypt_shards) {
    EncryptOutputFn encrypt_fn;
    absl::StatusOr<KVBlobStageOutputs> encrypt_out =
        DoFnRunner::RunReplicaWithKVBlobOutput(encrypt_fn, {.inputs = shard_inputs});
    if (!encrypt_out.ok()) {
      return encrypt_out.status();
    }
    result.encrypt_output_output.Append(*std::move(encrypt_out));
  }

  absl::StatusOr<EncryptOutputOutputs> split_encrypt_outputs =
      routers.encrypt_output_splitter(result.encrypt_output_output);
  if (!split_encrypt_outputs.ok()) {
    return split_encrypt_outputs.status();
  }

  // 9. KeyReleaser (VerifyFingerprintAndReleaseKey)
  // Replica requirement: Requires a single replica to aggregate all
  // PartialReleaseReports across every EncryptOutput shard and verify that the
  // complete release multiset fingerprint matches the canonical ingestion
  // fingerprint before releasing the Tink AEAD release_key.
  // Output: payload only. Emits a single unencrypted DecryptionKeyRelease.
  absl::StatusOr<KVCollection> kr_inputs = routers.key_releaser_router(
      verification_payload, split_encrypt_outputs->partial_release_reports);
  if (!kr_inputs.ok()) {
    return kr_inputs.status();
  }

  KeyReleaserFn kr_fn;
  absl::StatusOr<BlobStageOutputs> kr_out =
      DoFnRunner::RunReplicaWithBlobOutput(kr_fn, {.inputs = *kr_inputs});
  if (!kr_out.ok()) {
    return kr_out.status();
  }
  result.key_releaser_output = *std::move(kr_out);

  // 10. OrchestratorDecryptAndRoute
  // Decrypts every EncryptedMessageOutput using the released Tink AEAD key and
  // groups the customer messages by MetricEpochKey.
  absl::StatusOr<absl::flat_hash_map<MetricEpochKey, std::vector<std::string>>> released =
      routers.orchestrator_decrypt_and_route(result.key_releaser_output,
                                             split_encrypt_outputs->encrypted_message_outputs);
  if (!released.ok()) {
    return released.status();
  }
  result.released_customer_messages = *std::move(released);

  return result;
}

}  // namespace cobalt::trusted_shuffler::testing::integration
