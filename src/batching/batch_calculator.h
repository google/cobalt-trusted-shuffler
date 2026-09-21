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

#ifndef SRC_BATCHING_BATCH_CALCULATOR_H_
#define SRC_BATCHING_BATCH_CALCULATOR_H_

#include <cstdint>

#include "absl/status/statusor.h"
#include "src/crypto/verify_ingestion_verification_payload.h"
#include "src/proto/common.pb.h"

namespace cobalt::trusted_shuffler {

// Batching parameters calculated for a partition.
struct BatchParameters {
  // Target batch size configured in the signed run context.
  int32_t target_batch_size;

  // Actual batch size per worker:
  // ceil(message_count / batch_num) == (message_count + padding_count) / batch_num.
  // Each downstream scattering worker receives exactly actual_batch_size messages
  // (real messages plus dummy padding messages).
  int32_t actual_batch_size;

  // Number of batches and downstream scattering groups:
  // ceil(message_count / target_batch_size).
  int32_t batch_num;

  // Number of dummy padding messages added to the partition so that
  // message_count + padding_count == batch_num * actual_batch_size:
  // (remainder_count > 0) ? (batch_num - remainder_count) : 0, where
  // remainder_count = message_count % batch_num.
  int32_t padding_count;

  bool operator==(const BatchParameters&) const = default;
};

// Computes batch parameters for a specific partition from VerifiedIngestionVerificationResult.
//
// Validates:
// 1. result.run_context has target_batch_size > 0.
// 2. partition_key is an approved partition in result.approved_partitions.
// 3. partition_key exists in result.canonical_fingerprint with a positive message_count.
absl::StatusOr<BatchParameters> ComputeBatchParametersForPartition(
    const VerifiedIngestionVerificationResult& result, const MetricEpochKey& partition_key);

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_BATCHING_BATCH_CALCULATOR_H_
