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
#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/crypto/verify_ingestion_verification_payload.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler {
namespace {

absl::StatusOr<BatchParameters> ComputeBatchParameters(int64_t message_count,
                                                       int32_t target_batch_size) {
  if (target_batch_size <= 0) {
    return absl::InvalidArgumentError("target_batch_size must be positive.");
  }
  if (message_count <= 0) {
    return absl::InvalidArgumentError("message_count must be positive.");
  }

  const int64_t max_partition_messages =
      static_cast<int64_t>(target_batch_size) * static_cast<int64_t>(target_batch_size);
  if (message_count > max_partition_messages) {
    return absl::OutOfRangeError("message_count exceeds target_batch_size squared.");
  }

  // Integer ceiling division: ceil(message_count / target_batch_size).
  // Adding (target_batch_size - 1) before integer division rounds up any non-zero
  // remainder to the next integer while keeping exact multiples unchanged.
  const int32_t batch_num =
      static_cast<int32_t>((message_count + target_batch_size - 1) / target_batch_size);
  const int32_t remainder_count = static_cast<int32_t>(message_count % batch_num);
  const int32_t padding_count = (remainder_count > 0) ? (batch_num - remainder_count) : 0;
  const int64_t total_messages = message_count + padding_count;
  const int32_t actual_batch_size = static_cast<int32_t>(total_messages / batch_num);

  return BatchParameters{
      .target_batch_size = target_batch_size,
      .actual_batch_size = actual_batch_size,
      .batch_num = batch_num,
      .padding_count = padding_count,
  };
}

}  // namespace

absl::StatusOr<BatchParameters> ComputeBatchParametersForPartition(
    const VerifiedIngestionVerificationResult& result, const MetricEpochKey& partition_key) {
  // The approved partitions here are the same MetricEpochKeys that met the k-anonymity
  // threshold during ingestion verification, as recorded in ThresholdReport.
  // Checking this here provides defense-in-depth across the untrusted orchestrator
  // boundary: canonical_fingerprint records counts for all ingested partitions (including
  // unapproved ones), so this ensures a misbehaving orchestrator cannot route an
  // unapproved partition into downstream TEE stages (such as scatter_shuffle).
  if (!result.approved_partitions.contains(partition_key)) {
    return absl::FailedPreconditionError("Partition is not approved in ThresholdReport.");
  }

  const auto& entries = result.canonical_fingerprint.entries();
  const auto matching_entry = absl::c_find_if(entries, [&](const FingerprintEntry& entry) {
    return entry.metric_epoch_key() == partition_key;
  });

  if (matching_entry == entries.end()) {
    return absl::NotFoundError(
        "No matching entry found in canonical fingerprint for approved partition.");
  }

  return ComputeBatchParameters(matching_entry->message_count(),
                                result.run_context.run_config().target_batch_size());
}

int32_t GatherGroupIdForPosition(const int64_t position, const int32_t batch_num) {
  ABSL_DCHECK_GT(batch_num, 0);
  return static_cast<int32_t>(position % batch_num);
}

absl::StatusOr<int32_t> ComputeExpectedGatherGroupSizeForPartition(
    const VerifiedIngestionVerificationResult& result, const MetricEpochKey& partition_key,
    int32_t group_id) {
  absl::StatusOr<BatchParameters> params =
      ComputeBatchParametersForPartition(result, partition_key);
  if (!params.ok()) {
    return params.status();
  }

  const int32_t actual_batch_size = params->actual_batch_size;
  const int32_t batch_num = params->batch_num;
  if (group_id < 0 || group_id >= batch_num) {
    return absl::InvalidArgumentError("group_id must be in [0, batch_num).");
  }

  // Closed form of the number of positions in [0, actual_batch_size) that
  // GatherGroupIdForPosition maps to group_id, summed over the batch_num
  // equally sized scatter batches.
  const int32_t base_per_batch = actual_batch_size / batch_num;
  const int32_t remainder_per_batch = actual_batch_size % batch_num;
  const int64_t per_batch = base_per_batch + (group_id < remainder_per_batch ? 1 : 0);
  const int64_t expected_group_size = batch_num * per_batch;
  if (expected_group_size > std::numeric_limits<int32_t>::max()) {
    return absl::OutOfRangeError("Computed expected gather group size exceeds int32_t max.");
  }
  return static_cast<int32_t>(expected_group_size);
}

}  // namespace cobalt::trusted_shuffler
