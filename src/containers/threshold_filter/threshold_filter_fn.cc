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
#include "src/containers/base/counter_names.h"
#include "src/containers/base/integrity_fault.h"
#include "src/crypto/verify_ingestion_verification_payload.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler {

absl::Status ThresholdFilterFn::OnConfig(const IngestionVerificationPayload& config,
                                         DoContext& context) {
  context.IncrementCounter(kInputConfigCount);
  absl::StatusOr<VerifiedIngestionVerificationResult> verified_result =
      VerifyIngestionVerificationPayload(config);
  if (!verified_result.ok()) {
    context.IncrementCounter(kInputConfigInvalid);
    return fault_handler_.SignalFault(verified_result.status().message(), *this, context)
        .ToStatus();
  }

  run_context_ = std::move(verified_result->run_context);
  approved_partitions_ = std::move(verified_result->approved_partitions);
  has_verification_report_ = true;
  LOG(INFO) << "IngestionVerificationPayload received and validated in OnConfig.";

  return absl::OkStatus();
}

absl::Status ThresholdFilterFn::OnRecord(const ShuffledMessage& shuffled_message,
                                         DoContext& context) {
  context.IncrementCounter(kInputRecordCount);
  if (fault_handler_.HasFault()) {
    return fault_handler_
        .SignalFault("ThresholdFilterFn::OnRecord: Integrity fault occurred.", *this, context)
        .ToStatus();
  }

  if (!run_context_.has_value()) {
    return absl::FailedPreconditionError(
        "ThresholdFilterFn::OnRecord: configuration not established.");
  }

  if (!shuffled_message.has_metric_epoch_key()) {
    // TODO(b/555372765): Add separate counters for dropped due to thresholding vs invalid records.
    context.IncrementCounter(kInputRecordDropped);
    return absl::InvalidArgumentError(
        "ThresholdFilterFn::OnRecord: ShuffledMessage missing required MetricEpochKey.");
  }

  if (shuffled_message.run_id() != run_context_->run_id()) {
    return fault_handler_
        .SignalFault("ThresholdFilterFn::OnRecord: ShuffledMessage run_id mismatch.", *this,
                     context)
        .ToStatus();
  }

  if (shuffled_message.shuffle_count() != 0) {
    return fault_handler_
        .SignalFault("ThresholdFilterFn::OnRecord: ShuffledMessage shuffle_count must be 0.", *this,
                     context)
        .ToStatus();
  }

  if (approved_partitions_.contains(shuffled_message.metric_epoch_key())) {
    context.IncrementCounter(kEncryptedOutputRecordCount);
    EmitEncrypted(context, shuffled_message.metric_epoch_key(), shuffled_message).IgnoreError();
  } else {
    // TODO(b/555372765): Add separate counters for dropped due to thresholding vs invalid records.
    context.IncrementCounter(kInputRecordDropped);
  }
  // If not approved, message is discarded per specification.
  return absl::OkStatus();
}

absl::Status ThresholdFilterFn::OnFinalize(google::protobuf::Any config, FnContext& context) {
  if (!HasConfig()) {
    return absl::FailedPreconditionError(
        "ThresholdFilterFn::FinalizeReplica called before IngestionVerificationPayload "
        "configuration was established.");
  }
  if (fault_handler_.HasFault()) {
    return absl::FailedPreconditionError(
        "Cannot finalize ThresholdFilter replica due to upstream integrity fault.");
  }
  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler
