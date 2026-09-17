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

#include "absl/functional/any_invocable.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "cc/crypto/signing_key.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/integration/ingest_uploads_router.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

// Captures the host-visible outputs of every stage in a pipeline run. The type
// of each field states whether that stage keeps the routing key its records
// were emitted with.
struct PipelineExecutionResult {
  // Encrypted SignedRunContext payload emitted by RunContextInitializer.
  BlobStageOutputs run_context_initializer_output;

  // Encrypted partition messages and partial ingestion reports emitted across
  // all IngestUploads shards, each carrying its IngestMessagesHeader key.
  KVBlobStageOutputs ingest_uploads_output;
};

// Builds one input stream per IngestUploads shard from the SignedRunContext
// and the raw client upload checkpoints assigned to the stage.
using IngestUploadsRouterFn = absl::AnyInvocable<absl::StatusOr<std::vector<KVCollection>>(
    const EncryptedBlob& signed_run_context, absl::Span<const std::string> raw_checkpoints) const>;

// Reads the IngestMessagesHeader on each IngestUploads output to separate
// partial ingestion reports from partition messages.
using IngestUploadsOutputSplitterFn = absl::AnyInvocable<absl::StatusOr<IngestUploadsOutputs>(
    const KVBlobStageOutputs& ingest_output) const>;

// Holds routing and output-splitting functions used by ExecutePipeline to route
// data between container stages. Defaults to the standard router implementations,
// which tests can override to inject faults.
struct PipelineRouters {
  // Routes SignedRunContext and raw upload checkpoints into one or more IngestUploads shards.
  IngestUploadsRouterFn ingest_uploads_router = RouteToIngestUploads;

  // Demultiplexes IngestUploads outputs into partial reports and partition messages.
  IngestUploadsOutputSplitterFn ingest_uploads_output_splitter = SplitIngestUploadsOutputs;
};

// Executes the implemented container stages end-to-end using the provided
// routing functions.
absl::StatusOr<PipelineExecutionResult> ExecutePipeline(
    const PipelineRouters& routers, oak::crypto::SigningKeyHandle& signing_key,
    const RunConfig& run_config, absl::Span<const std::string> raw_checkpoints);

}  // namespace cobalt::trusted_shuffler::testing::integration

#endif  // SRC_TESTING_INTEGRATION_PIPELINE_RUNNER_H_
