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

#ifndef SRC_TESTING_INTEGRATION_INGEST_UPLOADS_ROUTER_H_
#define SRC_TESTING_INTEGRATION_INGEST_UPLOADS_ROUTER_H_

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

// Holds the separated outputs from an IngestUploads stage execution.
struct IngestUploadsOutputs {
  // Per-replica ingestion summaries destined for IngestionVerification, with the
  // IngestMessagesHeader routing key stripped.
  std::vector<EncryptedBlob> partial_reports;

  // Encrypted client messages destined for ThresholdFilter, with the
  // IngestMessagesHeader routing key stripped.
  std::vector<EncryptedBlob> partition_messages;
};

// Builds a single IngestUploads session input stream by prepending
// SignedRunContext as Record 0 followed by raw client upload checkpoints
// wrapped in BlobData as Records 1..N.
absl::StatusOr<std::vector<KVCollection>> RouteToIngestUploads(
    const EncryptedBlob& signed_run_context, absl::Span<const std::string> raw_checkpoints);

// Splits raw_checkpoints across two IngestUploads shards (replicas),
// prepending SignedRunContext as Record 0 for each shard.
absl::StatusOr<std::vector<KVCollection>> RouteToTwoShardIngestUploads(
    const EncryptedBlob& signed_run_context, absl::Span<const std::string> raw_checkpoints);

// Fault-injection router that places client upload checkpoints ahead of
// SignedRunContext instead of providing SignedRunContext as Record 0.
absl::StatusOr<std::vector<KVCollection>> RouteToIngestUploadsWithLateRunContext(
    const EncryptedBlob& signed_run_context, absl::Span<const std::string> raw_checkpoints);

// Fault-injection router that creates two shards where Shard 0 succeeds and
// Shard 1 places SignedRunContext last so that Shard 1 aborts.
absl::StatusOr<std::vector<KVCollection>> RouteToTwoShardIngestUploadsWithSecondShardAbort(
    const EncryptedBlob& signed_run_context, absl::Span<const std::string> raw_checkpoints);

// Unpacks each IngestMessagesHeader emitted by IngestUploads and separates outputs into:
// 1. partial_reports: records whose header holds a PartialIngestionReport.
// 2. partition_messages: records whose header holds a MetricEpochKey.
// Both streams strip the IngestMessagesHeader routing key. Returns
// FailedPreconditionError if any record header reports an IntegrityFault.
absl::StatusOr<IngestUploadsOutputs> SplitIngestUploadsOutputs(
    const KVBlobStageOutputs& ingest_output);

// Fault-injection splitter that drops the last PartialIngestionReport emitted
// by IngestUploads.
absl::StatusOr<IngestUploadsOutputs> SplitIngestUploadsOutputsWithDroppedReports(
    const KVBlobStageOutputs& ingest_output);

// Fault-injection splitter that injects an IntegrityFault header into the
// IngestUploads output stream before splitting.
absl::StatusOr<IngestUploadsOutputs> SplitIngestUploadsOutputsWithInjectedIntegrityFault(
    const KVBlobStageOutputs& ingest_output);

}  // namespace cobalt::trusted_shuffler::testing::integration

#endif  // SRC_TESTING_INTEGRATION_INGEST_UPLOADS_ROUTER_H_
