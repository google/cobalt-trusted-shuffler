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

#ifndef SRC_CRYPTO_VERIFY_INGESTION_VERIFICATION_PAYLOAD_H_
#define SRC_CRYPTO_VERIFY_INGESTION_VERIFICATION_PAYLOAD_H_

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler {

// Verified components extracted from an IngestionVerificationPayload.
struct VerifiedIngestionVerificationResult {
  RunContext run_context;
  absl::flat_hash_set<MetricEpochKey> approved_partitions;
  RunFingerprint canonical_fingerprint;
};

// Validates an IngestionVerificationResult:
// - `signed_run_context` must be present and verified via `VerifySignedRunContext`.
// - `threshold_report` must be present and its `run_id` must match the verified
//   `run_context.run_id()`.
// - Each partition in `threshold_report.approved_partitions()` must have valid
//   bounds: `project_id > 0`, `metric_id > 0`, and `epoch_index >= 0`.
// - If `canonical_fingerprint` is present, its `run_id` must match the verified
//   `run_context.run_id()`, its `stage_id` must be `STAGE_INGESTION`, and its
//   `is_partial` must be `false`.
// Returns a VerifiedIngestionVerificationResult containing the verified RunContext,
// deduplicated set of approved partitions, and canonical_fingerprint, or an error status on
// failure.
absl::StatusOr<VerifiedIngestionVerificationResult> VerifyIngestionVerificationResult(
    const IngestionVerificationResult& ingestion_verification_result);

// Validates an IngestionVerificationPayload:
// - Must not contain `integrity_fault` and must contain `success`
//   (`IngestionVerificationResult`).
// - `signed_run_context` must be present and verified via `VerifySignedRunContext`.
// - `threshold_report` must be present and its `run_id` must match the verified
//   `run_context.run_id()`.
// - Each partition in `threshold_report.approved_partitions()` must have valid
//   bounds: `project_id > 0`, `metric_id > 0`, and `epoch_index >= 0`.
// - `canonical_fingerprint` must be present, its `run_id` must match the verified
//   `run_context.run_id()`, its `stage_id` must be `STAGE_INGESTION`, and its
//   `is_partial` must be `false`.
// Returns a VerifiedIngestionVerificationResult containing the verified RunContext,
// deduplicated set of approved partitions, and verified canonical_fingerprint, or an
// error status on failure.
absl::StatusOr<VerifiedIngestionVerificationResult> VerifyIngestionVerificationPayload(
    const IngestionVerificationPayload& ingestion_verification_payload);

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CRYPTO_VERIFY_INGESTION_VERIFICATION_PAYLOAD_H_
