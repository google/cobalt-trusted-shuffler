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

#include "src/containers/threshold_filter/threshold_filter_fn.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "google/protobuf/any.pb.h"
#include "src/crypto/verify_ingestion_verification_payload.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler {

using ::cobalt::trusted_shuffler::RunContext;
using ::cobalt::trusted_shuffler::SignedRunContext;

void ThresholdFilterFn::EmitIntegrityFault(Context& context) {
  has_integrity_fault_ = true;
  IntegrityFault fault;
  google::protobuf::Any key;
  key.PackFrom(fault);
  context.EmitUnencrypted(KV(std::move(key), fault.SerializeAsString()));
}

absl::Status ThresholdFilterFn::OnConfig(const IngestionVerificationPayload& config,
                                         Context& context) {
  if (has_integrity_fault_ || config.has_integrity_fault()) {
    EmitIntegrityFault(context);
    return absl::FailedPreconditionError("Ingestion verification failed with integrity fault.");
  }

  if (!config.has_success()) {
    return absl::InvalidArgumentError(
        "IngestionVerificationPayload must contain either success (threshold_report) or "
        "integrity_fault.");
  }

  absl::StatusOr<VerifiedIngestionVerificationResult> verified_result =
      VerifyIngestionVerificationResult(config.success());
  if (!verified_result.ok()) {
    return verified_result.status();
  }

  run_context_ = std::move(verified_result->run_context);
  signed_run_context_ = config.success().signed_run_context();
  approved_partitions_ = std::move(verified_result->approved_partitions);
  has_verification_report_ = true;
  LOG(INFO) << "SignedRunContext received and validated in OnConfig.";

  return absl::OkStatus();
}

absl::Status ThresholdFilterFn::OnRecord(KV record, Context& context) {
  if (has_integrity_fault_) {
    EmitIntegrityFault(context);
    return absl::FailedPreconditionError("ThresholdFilterFn::OnRecord: Integrity fault occurred.");
  }

  if (!run_context_.has_value()) {
    return absl::FailedPreconditionError(
        "ThresholdFilterFn::OnRecord: configuration not established.");
  }

  ShuffledMessage shuffled_message;
  if (!shuffled_message.ParseFromString(record.data)) {
    return absl::InvalidArgumentError(
        "ThresholdFilterFn::OnRecord: record.data cannot be parsed as ShuffledMessage.");
  }

  if (!shuffled_message.has_metric_epoch_key()) {
    return absl::InvalidArgumentError(
        "ThresholdFilterFn::OnRecord: ShuffledMessage missing required MetricEpochKey.");
  }

  if (shuffled_message.run_id() != run_context_->run_id()) {
    EmitIntegrityFault(context);
    return absl::FailedPreconditionError(
        "ThresholdFilterFn::OnRecord: ShuffledMessage run_id mismatch.");
  }

  if (shuffled_message.shuffle_count() != 0) {
    EmitIntegrityFault(context);
    return absl::FailedPreconditionError(
        "ThresholdFilterFn::OnRecord: ShuffledMessage shuffle_count must be 0.");
  }

  if (approved_partitions_.contains(shuffled_message.metric_epoch_key())) {
    google::protobuf::Any key;
    key.PackFrom(shuffled_message.metric_epoch_key());
    context.EmitEncrypted(kReencryptionKeyIndex, KV(std::move(key), std::move(record.data)));
  }
  // If not approved, message is discarded per specification.
  return absl::OkStatus();
}

absl::Status ThresholdFilterFn::FinalizeReplica(google::protobuf::Any config, Context& context) {
  if (!HasConfig()) {
    return absl::FailedPreconditionError(
        "ThresholdFilterFn::FinalizeReplica called before IngestionVerificationPayload "
        "configuration was established.");
  }
  if (has_integrity_fault_) {
    return absl::FailedPreconditionError(
        "Cannot finalize ThresholdFilter replica due to upstream integrity fault.");
  }
  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler
