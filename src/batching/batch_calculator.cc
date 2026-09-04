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

#include "src/batching/batch_calculator.h"

#include <cstdint>
#include <limits>

#include "absl/algorithm/container.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/crypto/verify_signed_run_context.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler {
namespace {

absl::StatusOr<int32_t> ExtractTargetBatchSize(const IngestionVerificationResult& result) {
  if (!result.has_signed_run_context()) {
    return absl::InvalidArgumentError(
        "IngestionVerificationResult missing required signed_run_context.");
  }

  absl::StatusOr<RunContext> run_context = VerifySignedRunContext(result.signed_run_context());
  if (!run_context.ok()) {
    return run_context.status();
  }

  return run_context->run_config().target_batch_size();
}

absl::StatusOr<BatchParameters> ComputeBatchParameters(int64_t message_count,
                                                       int32_t target_batch_size) {
  if (target_batch_size <= 0) {
    return absl::InvalidArgumentError("target_batch_size must be positive.");
  }
  if (message_count <= 0) {
    return absl::InvalidArgumentError("message_count must be positive.");
  }

  // Integer ceiling division: ceil(message_count / target_batch_size).
  // Adding (target_batch_size - 1) before integer division rounds up any non-zero
  // remainder to the next integer while keeping exact multiples unchanged.
  const int64_t batch_num_64 = (message_count + target_batch_size - 1) / target_batch_size;
  if (batch_num_64 > std::numeric_limits<int32_t>::max()) {
    return absl::OutOfRangeError("Computed batch_num exceeds int32_t max.");
  }
  const int32_t batch_num = static_cast<int32_t>(batch_num_64);
  const int32_t remainder_count = static_cast<int32_t>(message_count % target_batch_size);
  const int32_t padding_count = (target_batch_size - remainder_count) % target_batch_size;

  return BatchParameters{
      .target_batch_size = target_batch_size,
      .batch_num = batch_num,
      .padding_count = padding_count,
  };
}

}  // namespace

absl::StatusOr<BatchParameters> ComputeBatchParametersForPartition(
    const IngestionVerificationResult& result, const MetricEpochKey& partition_key) {
  absl::StatusOr<int32_t> target_batch_size = ExtractTargetBatchSize(result);
  if (!target_batch_size.ok()) {
    return target_batch_size.status();
  }

  // The approved partitions here are the same MetricEpochKeys that met the k-anonymity
  // threshold during ingestion verification, as recorded in ThresholdReport.
  // Checking this here provides defense-in-depth across the untrusted orchestrator
  // boundary: canonical_fingerprint records counts for all ingested partitions (including
  // unapproved ones), so this ensures a misbehaving orchestrator cannot route an
  // unapproved partition into downstream TEE stages (such as scatter_shuffle).
  const auto& approved_partitions = result.threshold_report().approved_partitions();
  const bool is_approved = absl::c_any_of(
      approved_partitions, [&](const MetricEpochKey& key) { return key == partition_key; });

  if (!is_approved) {
    return absl::FailedPreconditionError("Partition is not approved in ThresholdReport.");
  }

  const auto& entries = result.canonical_fingerprint().entries();
  const auto matching_entry = absl::c_find_if(entries, [&](const FingerprintEntry& entry) {
    return entry.metric_epoch_key() == partition_key;
  });

  if (matching_entry == entries.end()) {
    return absl::NotFoundError(
        "No matching entry found in canonical fingerprint for approved partition.");
  }

  return ComputeBatchParameters(matching_entry->message_count(), *target_batch_size);
}

}  // namespace cobalt::trusted_shuffler
