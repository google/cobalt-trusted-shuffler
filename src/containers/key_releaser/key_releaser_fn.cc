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

#include "src/containers/key_releaser/key_releaser_fn.h"

#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "src/containers/base/counter_names.h"
#include "src/crypto/multiset_fingerprint.h"
#include "src/crypto/verify_ingestion_verification_payload.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/release.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler {

// Execution context passed to per-record Do invocations.
using DoContext = confidential_federated_compute::fns::DoFn::DoContext;

// Session context passed to replica initialization and finalization hooks.
using FnContext = confidential_federated_compute::fns::Fn::FnContext;

namespace {

absl::Status ValidatePartialReleaseFingerprint(
    const RunFingerprint& partial_fingerprint,
    const absl::flat_hash_set<MetricEpochKey>& approved_partitions) {
  absl::flat_hash_set<MetricEpochKey> seen_partitions;
  for (const FingerprintEntry& entry : partial_fingerprint.entries()) {
    if (!entry.has_metric_epoch_key()) {
      return absl::InvalidArgumentError("FingerprintEntry missing metric_epoch_key.");
    }
    const MetricEpochKey& key = entry.metric_epoch_key();
    if (key.project_id() <= 0 || key.metric_id() <= 0 || key.epoch_index() < 0) {
      return absl::InvalidArgumentError("Invalid MetricEpochKey in partial_fingerprint.");
    }
    if (!approved_partitions.contains(key)) {
      return absl::NotFoundError("Unapproved partition in partial_fingerprint.");
    }
    if (!seen_partitions.insert(key).second) {
      return absl::AlreadyExistsError("Duplicate partition in partial_fingerprint.");
    }
    if (entry.message_count() < 0) {
      return absl::InvalidArgumentError("Negative message_count in partial_fingerprint.");
    }
    if (entry.message_count() == 0) {
      if (!entry.fingerprint().empty() &&
          entry.fingerprint() != std::string(MultisetFingerprint::kFingerprintSize, '\0')) {
        return absl::InvalidArgumentError(
            "Non-zero fingerprint for zero message_count in partial_fingerprint.");
      }
    } else if (entry.fingerprint().size() != MultisetFingerprint::kFingerprintSize) {
      return absl::InvalidArgumentError("Invalid fingerprint size in partial_fingerprint.");
    }
  }
  return absl::OkStatus();
}

}  // namespace

absl::Status KeyReleaserFn::OnConfig(const IngestionVerificationPayload& config,
                                     DoContext& context) {
  context.IncrementCounter(kInputConfigCount);
  if (is_finalized_) {
    context.IncrementCounter(kInputConfigInvalid);
    return absl::FailedPreconditionError("Replica is already finalized.");
  }
  if (has_integrity_fault_) {
    context.IncrementCounter(kInputConfigInvalid);
    return EmitIntegrityFault("Replica previously encountered an integrity fault.", context);
  }
  absl::StatusOr<VerifiedIngestionVerificationResult> verified =
      VerifyIngestionVerificationPayload(config);
  if (!verified.ok()) {
    context.IncrementCounter(kInputConfigInvalid);
    return EmitIntegrityFault(verified.status().message(), context);
  }

  if (verified->run_context.release_key().empty()) {
    context.IncrementCounter(kInputConfigInvalid);
    return EmitIntegrityFault("RunContext missing release_key.", context);
  }

  absl::flat_hash_set<MetricEpochKey> seen_fingerprint_partitions;
  for (const FingerprintEntry& entry : verified->canonical_fingerprint.entries()) {
    seen_fingerprint_partitions.insert(entry.metric_epoch_key());
  }
  if (seen_fingerprint_partitions != verified->approved_partitions) {
    context.IncrementCounter(kInputConfigInvalid);
    return EmitIntegrityFault("canonical_fingerprint partitions do not match approved_partitions.",
                              context);
  }

  run_context_ = std::move(verified->run_context);
  approved_partitions_ = std::move(verified->approved_partitions);
  canonical_fingerprint_ = std::move(verified->canonical_fingerprint);
  multiset_fingerprint_.emplace(Stage::STAGE_RELEASE, /*is_partial=*/false, *run_context_,
                                MultisetFingerprint::kFingerprintSize);
  return absl::OkStatus();
}

absl::Status KeyReleaserFn::OnRecord(const PartialReleaseReport& report, DoContext& context) {
  context.IncrementCounter(kInputRecordCount);
  if (is_finalized_) {
    return absl::FailedPreconditionError("Replica is already finalized.");
  }
  if (has_integrity_fault_) {
    return EmitIntegrityFault("Replica previously encountered an integrity fault.", context);
  }

  if (!report.has_partial_fingerprint() ||
      !ValidatePartialReleaseFingerprint(report.partial_fingerprint(), approved_partitions_).ok()) {
    return EmitIntegrityFault("Invalid PartialReleaseReport in KeyReleaserFn.", context);
  }

  absl::Status merge_status = multiset_fingerprint_->Merge(report.partial_fingerprint());
  if (!merge_status.ok()) {
    return EmitIntegrityFault("Failed to merge partial release fingerprint in KeyReleaserFn.",
                              context);
  }

  return absl::OkStatus();
}

absl::Status KeyReleaserFn::EmitIntegrityFault(absl::string_view reason, FnContext& context) {
  has_integrity_fault_ = true;
  LOG(ERROR) << "IntegrityFault in KeyReleaserFn: " << reason;
  if (!has_emitted_output_) {
    has_emitted_output_ = true;
    DecryptionKeyRelease release_msg;
    release_msg.mutable_integrity_fault();

    context.IncrementCounter(kUnencryptedOutputRecordCount);
    EmitUnencrypted(context, release_msg).IgnoreError();
  }
  return absl::FailedPreconditionError(reason);
}

absl::Status KeyReleaserFn::FinalizeReplica(google::protobuf::Any config, FnContext& context) {
  if (is_finalized_) {
    return absl::FailedPreconditionError("Replica is already finalized.");
  }
  is_finalized_ = true;

  if (!HasConfig() || has_integrity_fault_ || !multiset_fingerprint_.has_value() ||
      !canonical_fingerprint_.has_value()) {
    return EmitIntegrityFault(
        "Release verification failed due to missing configuration or integrity fault.", context);
  }

  if (!multiset_fingerprint_->VerifyComplete(*canonical_fingerprint_)) {
    return EmitIntegrityFault(
        "Release multiset fingerprint does not match canonical ingestion fingerprint.", context);
  }

  has_emitted_output_ = true;
  DecryptionKeyRelease release_msg;
  release_msg.mutable_released_key_report()->set_release_key(run_context_->release_key());

  context.IncrementCounter(kUnencryptedOutputRecordCount);
  EmitUnencrypted(context, release_msg).IgnoreError();
  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler
