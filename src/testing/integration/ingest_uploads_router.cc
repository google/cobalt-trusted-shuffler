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

#include "src/testing/integration/ingest_uploads_router.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "containers/session.h"
#include "google/protobuf/any.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {
namespace {

KVCollection BuildShardInputs(const EncryptedBlob& signed_run_context,
                              absl::Span<const std::string> checkpoints) {
  KVCollection inputs;
  inputs.reserve(1 + checkpoints.size());
  inputs.push_back(signed_run_context.ToKV());

  for (const std::string& checkpoint : checkpoints) {
    inputs.push_back(confidential_federated_compute::Session::KV(checkpoint));
  }

  return inputs;
}

}  // namespace

absl::StatusOr<std::vector<KVCollection>> RouteToIngestUploads(
    const EncryptedBlob& signed_run_context, absl::Span<const std::string> raw_checkpoints) {
  return std::vector<KVCollection>{
      BuildShardInputs(signed_run_context, raw_checkpoints),
  };
}

absl::StatusOr<std::vector<KVCollection>> RouteToTwoShardIngestUploads(
    const EncryptedBlob& signed_run_context, absl::Span<const std::string> raw_checkpoints) {
  const size_t mid = raw_checkpoints.size() / 2;
  return std::vector<KVCollection>{
      BuildShardInputs(signed_run_context, raw_checkpoints.subspan(0, mid)),
      BuildShardInputs(signed_run_context, raw_checkpoints.subspan(mid)),
  };
}

absl::StatusOr<std::vector<KVCollection>> RouteToIngestUploadsWithLateRunContext(
    const EncryptedBlob& signed_run_context, absl::Span<const std::string> raw_checkpoints) {
  KVCollection inputs;
  inputs.reserve(1 + raw_checkpoints.size());

  for (const std::string& checkpoint : raw_checkpoints) {
    inputs.push_back(confidential_federated_compute::Session::KV(checkpoint));
  }
  inputs.push_back(signed_run_context.ToKV());

  return std::vector<KVCollection>{std::move(inputs)};
}

absl::StatusOr<std::vector<KVCollection>> RouteToTwoShardIngestUploadsWithSecondShardAbort(
    const EncryptedBlob& signed_run_context, absl::Span<const std::string> raw_checkpoints) {
  const size_t mid = raw_checkpoints.size() / 2;
  KVCollection shard_1;
  shard_1.reserve(1 + (raw_checkpoints.size() - mid));
  for (const std::string& checkpoint : raw_checkpoints.subspan(mid)) {
    shard_1.push_back(confidential_federated_compute::Session::KV(checkpoint));
  }
  shard_1.push_back(signed_run_context.ToKV());

  return std::vector<KVCollection>{
      BuildShardInputs(signed_run_context, raw_checkpoints.subspan(0, mid)),
      std::move(shard_1),
  };
}

absl::StatusOr<IngestUploadsOutputs> SplitIngestUploadsOutputs(
    const KVBlobStageOutputs& ingest_output) {
  IngestUploadsOutputs split_outputs;

  for (const EncryptedKVBlob& record : ingest_output.encrypted) {
    IngestMessagesHeader header;
    if (!record.key().UnpackTo(&header)) {
      return absl::InvalidArgumentError(
          "Failed to unpack IngestMessagesHeader from IngestUploads output key.");
    }

    if (header.has_integrity_fault()) {
      return absl::FailedPreconditionError("IngestUploads reported an integrity fault.");
    } else if (header.has_partial_ingestion_report()) {
      split_outputs.partial_reports.push_back(record.DropKey());
    } else if (header.has_metric_epoch_key()) {
      split_outputs.ingested_messages.push_back(record.DropKey());
    } else {
      return absl::InvalidArgumentError(
          "IngestMessagesHeader has neither partial_ingestion_report, metric_epoch_key, nor "
          "integrity_fault.");
    }
  }

  return split_outputs;
}

absl::StatusOr<IngestUploadsOutputs> SplitIngestUploadsOutputsWithDroppedReports(
    const KVBlobStageOutputs& ingest_output) {
  absl::StatusOr<IngestUploadsOutputs> split_outputs = SplitIngestUploadsOutputs(ingest_output);
  if (split_outputs.ok() && !split_outputs->partial_reports.empty()) {
    split_outputs->partial_reports.pop_back();
  }
  return split_outputs;
}

absl::StatusOr<IngestUploadsOutputs> SplitIngestUploadsOutputsWithInjectedIntegrityFault(
    const KVBlobStageOutputs& ingest_output) {
  KVBlobStageOutputs mutated_output = ingest_output;
  IngestMessagesHeader fault_header;
  fault_header.mutable_integrity_fault();
  google::protobuf::Any fault_key;
  fault_key.PackFrom(fault_header);
  mutated_output.encrypted.emplace_back(std::move(fault_key), "");
  return SplitIngestUploadsOutputs(mutated_output);
}

}  // namespace cobalt::trusted_shuffler::testing::integration
