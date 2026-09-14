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

#include "src/crypto/verify_ingestion_verification_payload.h"

#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/crypto/verify_signed_run_context.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler {

absl::StatusOr<VerifiedIngestionVerificationResult> VerifyIngestionVerificationResult(
    const IngestionVerificationResult& ingestion_verification_result) {
  if (!ingestion_verification_result.has_signed_run_context()) {
    return absl::InvalidArgumentError("IngestionVerificationResult missing signed_run_context.");
  }

  absl::StatusOr<RunContext> verified_run_context =
      VerifySignedRunContext(ingestion_verification_result.signed_run_context());
  if (!verified_run_context.ok()) {
    return absl::InvalidArgumentError("Failed to verify SignedRunContext.");
  }

  if (!ingestion_verification_result.has_threshold_report()) {
    return absl::InvalidArgumentError("IngestionVerificationResult missing threshold_report.");
  }

  const ThresholdReport& threshold_report = ingestion_verification_result.threshold_report();
  if (threshold_report.run_id() != verified_run_context->run_id()) {
    return absl::FailedPreconditionError("ThresholdReport run_id mismatch.");
  }

  absl::flat_hash_set<MetricEpochKey> approved_partitions;
  for (const auto& partition : threshold_report.approved_partitions()) {
    if (partition.project_id() <= 0 || partition.metric_id() <= 0 || partition.epoch_index() < 0) {
      return absl::InvalidArgumentError("Invalid MetricEpochKey in approved_partitions.");
    }
    approved_partitions.insert(partition);
  }

  if (ingestion_verification_result.has_canonical_fingerprint()) {
    const RunFingerprint& canonical_fingerprint =
        ingestion_verification_result.canonical_fingerprint();
    if (canonical_fingerprint.run_id() != verified_run_context->run_id()) {
      return absl::FailedPreconditionError("RunFingerprint run_id mismatch.");
    }
    if (canonical_fingerprint.stage_id() != STAGE_INGESTION) {
      return absl::InvalidArgumentError("RunFingerprint stage_id must be STAGE_INGESTION.");
    }
    if (canonical_fingerprint.is_partial()) {
      return absl::InvalidArgumentError("RunFingerprint is_partial must be false.");
    }
  }

  return VerifiedIngestionVerificationResult{
      .run_context = *std::move(verified_run_context),
      .approved_partitions = std::move(approved_partitions),
      .canonical_fingerprint = ingestion_verification_result.canonical_fingerprint(),
  };
}

absl::StatusOr<VerifiedIngestionVerificationResult> VerifyIngestionVerificationPayload(
    const IngestionVerificationPayload& ingestion_verification_payload) {
  if (ingestion_verification_payload.has_integrity_fault()) {
    return absl::FailedPreconditionError("Ingestion verification failed with integrity fault.");
  }

  if (!ingestion_verification_payload.has_success()) {
    return absl::InvalidArgumentError("IngestionVerificationPayload missing success result.");
  }

  const IngestionVerificationResult& ingestion_verification_result =
      ingestion_verification_payload.success();

  absl::StatusOr<VerifiedIngestionVerificationResult> verified_result =
      VerifyIngestionVerificationResult(ingestion_verification_result);
  if (!verified_result.ok()) {
    return verified_result.status();
  }

  if (!ingestion_verification_result.has_canonical_fingerprint()) {
    return absl::InvalidArgumentError("IngestionVerificationResult missing canonical_fingerprint.");
  }

  return verified_result;
}

}  // namespace cobalt::trusted_shuffler
