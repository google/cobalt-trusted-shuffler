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

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "cc/crypto/signing_key.h"
#include "src/proto/common.pb.h"
#include "src/proto/config_constraints.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/integration/encrypt_output_router.h"
#include "src/testing/integration/gather_shuffle_router.h"
#include "src/testing/integration/ingest_uploads_router.h"
#include "src/testing/integration/ingestion_verification_router.h"
#include "src/testing/integration/key_releaser_router.h"
#include "src/testing/integration/pre_padding_router.h"
#include "src/testing/integration/records.h"
#include "src/testing/integration/scatter_shuffle_router.h"
#include "src/testing/integration/threshold_filter_router.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler::testing::integration {

// Captures the host-visible outputs of every stage in a pipeline run. The type
// of each field states whether that stage keeps the routing key its records
// were emitted with.
struct PipelineExecutionResult {
  // Encrypted SignedRunContext payload emitted by RunContextInitializer.
  BlobStageOutputs run_context_initializer_output;

  // Encrypted ingested messages and partial ingestion reports emitted across
  // all IngestUploads shards, each carrying its IngestMessagesHeader key.
  KVBlobStageOutputs ingest_uploads_output;

  // Encrypted IngestionVerificationPayload emitted by IngestionVerification.
  KVBlobStageOutputs ingestion_verification_output;

  // Encrypted ShuffledMessage records approved by ThresholdFilter, each keyed
  // by MetricEpochKey.
  KVBlobStageOutputs threshold_filter_output;

  // Encrypted padding messages emitted by PrePadding, keyed by MetricEpochKey.
  KVBlobStageOutputs pre_padding_output;

  // Encrypted ShuffledMessage records emitted by ScatterShuffle, keyed by
  // ScatterShuffleHeader. Unencrypted integrity fault records on failure.
  KVBlobStageOutputs scatter_shuffle_output;

  // Encrypted ShuffledMessage records emitted by GatherShuffle after the second
  // shuffling pass.
  BlobStageOutputs gather_shuffle_output;

  // Unencrypted EncryptedMessageOutput records and encrypted PartialReleaseReport
  // records emitted across all EncryptOutput shards, keyed by ReleaseOutputHeader.
  KVBlobStageOutputs encrypt_output_output;

  // Unencrypted DecryptionKeyRelease payload emitted by KeyReleaser.
  BlobStageOutputs key_releaser_output;

  // Decrypted customer message payloads grouped by MetricEpochKey after the
  // orchestrator decrypts EncryptedMessageOutput records with the released key.
  absl::flat_hash_map<MetricEpochKey, std::vector<std::string>> released_customer_messages;
};

// Builds one input stream per IngestUploads shard from the SignedRunContext
// and the raw client upload checkpoints assigned to the stage.
using IngestUploadsRouterFn = absl::AnyInvocable<absl::StatusOr<std::vector<KVCollection>>(
    const EncryptedBlob& signed_run_context, absl::Span<const std::string> raw_checkpoints) const>;

// Reads the IngestMessagesHeader on each IngestUploads output to separate
// partial ingestion reports from ingested messages.
using IngestUploadsOutputSplitterFn = absl::AnyInvocable<absl::StatusOr<IngestUploadsOutputs>(
    const KVBlobStageOutputs& ingest_output) const>;

// Builds the input stream for the IngestionVerification session from the
// SignedRunContext and every partial report the pipeline produced.
using IngestionVerificationRouterFn = absl::AnyInvocable<absl::StatusOr<KVCollection>(
    const EncryptedBlob& signed_run_context, absl::Span<const EncryptedBlob> partial_reports)
                                                             const>;

// Builds the input stream for a ThresholdFilter session from the
// IngestionVerificationPayload and the ingested messages assigned to it.
using ThresholdFilterRouterFn = absl::AnyInvocable<absl::StatusOr<KVCollection>(
    const EncryptedBlob& verification_payload, absl::Span<const EncryptedBlob> ingested_messages)
                                                       const>;

// Builds the input stream for a PrePadding session from the IngestionVerificationPayload.
using PrePaddingRouterFn = absl::AnyInvocable<absl::StatusOr<KVCollection>(
    const EncryptedBlob& verification_payload) const>;

// Builds the input stream for a ScatterShuffle session from the IngestionVerificationPayload,
// real (ThresholdFilter) and padding (PrePadding) messages.
using ScatterShuffleRouterFn = absl::AnyInvocable<absl::StatusOr<std::vector<KVCollection>>(
    const EncryptedBlob& verification_payload, int32_t target_batch_size,
    absl::Span<const EncryptedKVBlob> threshold_filter_messages,
    absl::Span<const EncryptedKVBlob> pre_padding_messages) const>;

// Routes IngestionVerificationPayload and ScatterShuffle messages into GatherShuffle.
using GatherShuffleRouterFn = absl::AnyInvocable<absl::StatusOr<std::vector<KVCollection>>(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedKVBlob> scatter_shuffle_messages) const>;

// Routes IngestionVerificationPayload and gathered messages into one or more
// EncryptOutput shards.
using EncryptOutputRouterFn = absl::AnyInvocable<absl::StatusOr<std::vector<KVCollection>>(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedBlob> gathered_messages) const>;

// Demultiplexes EncryptOutput records by ReleaseOutputHeader into
// EncryptedMessageOutput records and PartialReleaseReports.
using EncryptOutputSplitterFn = absl::AnyInvocable<absl::StatusOr<EncryptOutputOutputs>(
    const KVBlobStageOutputs& encrypt_output) const>;

// Routes IngestionVerificationPayload and PartialReleaseReports into the
// KeyReleaser session.
using KeyReleaserRouterFn = absl::AnyInvocable<absl::StatusOr<KVCollection>(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedBlob> partial_release_reports) const>;

// Decrypts EncryptedMessageOutput records using the released DecryptionKeyRelease
// and groups customer messages by MetricEpochKey.
using OrchestratorDecryptAndRouteFn = absl::AnyInvocable<
    absl::StatusOr<absl::flat_hash_map<MetricEpochKey, std::vector<std::string>>>(
        const BlobStageOutputs& key_releaser_output,
        absl::Span<const UnencryptedBlob> encrypted_message_outputs) const>;

// Holds routing and output-splitting functions used by ExecutePipeline to route
// data between container stages. Defaults to the standard router implementations,
// which tests can override to inject faults.
struct PipelineRouters {
  // Routes SignedRunContext and raw upload checkpoints into one or more IngestUploads shards.
  IngestUploadsRouterFn ingest_uploads_router = RouteToIngestUploads;

  // Demultiplexes IngestUploads outputs into partial reports and ingested messages.
  IngestUploadsOutputSplitterFn ingest_uploads_output_splitter = SplitIngestUploadsOutputs;

  // Routes SignedRunContext and partial reports into the IngestionVerification session.
  IngestionVerificationRouterFn ingestion_verification_router = RouteToIngestionVerification;

  // Routes IngestionVerificationPayload and ingested messages into the ThresholdFilter session.
  ThresholdFilterRouterFn threshold_filter_router = RouteToThresholdFilter;

  // Routes IngestionVerificationPayload into the PrePadding session.
  PrePaddingRouterFn pre_padding_router = RouteToPrePadding;

  // Routes IngestionVerificationPayload, ThresholdFilter and PrePadding messages into
  // ScatterShuffle.
  ScatterShuffleRouterFn scatter_shuffle_router = RouteToScatterShuffle;

  // Routes IngestionVerificationPayload and ScatterShuffle messages into GatherShuffle.
  GatherShuffleRouterFn gather_shuffle_router = RouteToGatherShuffle;

  // Routes IngestionVerificationPayload and GatherShuffle messages into EncryptOutput.
  EncryptOutputRouterFn encrypt_output_router = RouteToEncryptOutput;

  // Demultiplexes EncryptOutput records into EncryptedMessageOutputs and PartialReleaseReports.
  EncryptOutputSplitterFn encrypt_output_splitter = SplitEncryptOutputOutputs;

  // Routes IngestionVerificationPayload and PartialReleaseReports into KeyReleaser.
  KeyReleaserRouterFn key_releaser_router = RouteToKeyReleaser;

  // Decrypts EncryptedMessageOutputs with the released key and groups by MetricEpochKey.
  OrchestratorDecryptAndRouteFn orchestrator_decrypt_and_route = DecryptAndRouteReleasedMessages;
};

// Executes the complete 9-stage Trusted Shuffler pipeline end-to-end using the
// provided routing functions.
absl::StatusOr<PipelineExecutionResult> ExecutePipeline(
    const PipelineRouters& routers, oak::crypto::SigningKeyHandle& signing_key,
    const RunConfig& run_config, const TrustedShufflerConfigConstraints& constraints,
    absl::Span<const std::string> raw_checkpoints);

}  // namespace cobalt::trusted_shuffler::testing::integration

#endif  // SRC_TESTING_INTEGRATION_PIPELINE_RUNNER_H_
