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
#include "src/crypto/verify_signed_run_context.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler {

using ::cobalt::trusted_shuffler::RunContext;
using ::cobalt::trusted_shuffler::SignedRunContext;

absl::Status ThresholdFilterFn::OnConfig(const IngestionVerificationPayload& config,
                                         Context& context) {
  if (config.has_integrity_fault()) {
    has_integrity_fault_ = true;
    return absl::FailedPreconditionError("Ingestion verification failed with integrity fault.");
  }

  if (!config.has_success()) {
    return absl::InvalidArgumentError(
        "IngestionVerificationPayload must contain either success (threshold_report) or "
        "integrity_fault.");
  }

  const IngestionVerificationResult& result = config.success();
  if (!result.has_signed_run_context()) {
    return absl::InvalidArgumentError(
        "IngestionVerificationResult must contain signed_run_context.");
  }

  absl::StatusOr<::cobalt::trusted_shuffler::RunContext> run_context =
      VerifySignedRunContext(result.signed_run_context());
  if (!run_context.ok()) {
    return run_context.status();
  }
  run_context_ = std::move(*run_context);
  signed_run_context_ = result.signed_run_context();
  LOG(INFO) << "SignedRunContext received and validated in OnConfig.";

  const ThresholdReport& report = result.threshold_report();
  if (report.run_id() != run_context_->run_id()) {
    return absl::FailedPreconditionError("ThresholdReport run_id mismatch.");
  }

  for (const auto& partition : report.approved_partitions()) {
    approved_partitions_.insert(partition);
  }
  has_verification_report_ = true;

  return absl::OkStatus();
}

absl::Status ThresholdFilterFn::OnRecord(KV record, Context& context) {
  MetricEpochKey metric_epoch_key;
  if (!record.key.UnpackTo(&metric_epoch_key)) {
    return absl::InvalidArgumentError(
        "ThresholdFilterFn::OnRecord: record.key cannot be unpacked as MetricEpochKey.");
  }

  ShuffledMessage shuffled_message;
  if (!shuffled_message.ParseFromString(record.data)) {
    return absl::InvalidArgumentError(
        "ThresholdFilterFn::OnRecord: record.data cannot be parsed as ShuffledMessage.");
  }

  if (approved_partitions_.contains(metric_epoch_key)) {
    context.EmitEncrypted(kReencryptionKeyIndex, std::move(record));
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
