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

#include "src/containers/pre_padding/pre_padding_fn.h"

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/batching/batch_calculator.h"
#include "src/containers/base/counter_names.h"

namespace cobalt::trusted_shuffler {

absl::Status PrePaddingFn::OnConfig(const IngestionVerificationPayload& config,
                                    DoContext& context) {
  context.IncrementCounter(kInputConfigCount);
  if (is_finalized_) {
    context.IncrementCounter(kInputConfigInvalid);
    return absl::FailedPreconditionError("Replica is already finalized.");
  }
  if (fault_handler_.HasFault()) {
    return fault_handler_.SignalFault(*this, context).ToStatus();
  }
  absl::StatusOr<VerifiedIngestionVerificationResult> result =
      VerifyIngestionVerificationPayload(config);
  if (!result.ok()) {
    context.IncrementCounter(kInputConfigInvalid);
    return fault_handler_.SignalFault(result.status().message(), *this, context).ToStatus();
  }
  verified_result_ = *std::move(result);
  return absl::OkStatus();
}

absl::Status PrePaddingFn::OnRecord(const ShuffledMessage& /*input*/, DoContext& context) {
  context.IncrementCounter(kInputRecordCount);
  context.IncrementCounter(kInputRecordDropped);
  if (is_finalized_) {
    return absl::FailedPreconditionError("Replica is already finalized.");
  }
  if (fault_handler_.HasFault()) {
    return fault_handler_.SignalFault(*this, context).ToStatus();
  }
  return fault_handler_
      .SignalFault("This session is config-only and does not accept data records.", *this, context)
      .ToStatus();
}

absl::Status PrePaddingFn::FinalizeReplica(google::protobuf::Any /*config*/, FnContext& context) {
  if (is_finalized_) {
    return absl::FailedPreconditionError("Replica is already finalized.");
  }
  is_finalized_ = true;

  if (fault_handler_.HasFault()) {
    return fault_handler_.SignalFault(*this, context).ToStatus();
  }

  if (!verified_result_.has_value()) {
    return fault_handler_
        .SignalFault(
            "PrePaddingFn::FinalizeReplica called before IngestionVerificationPayload configuration "
            "was established.",
            *this, context)
        .ToStatus();
  }

  for (const MetricEpochKey& partition : verified_result_->approved_partitions) {
    absl::StatusOr<BatchParameters> batch_params =
        ComputeBatchParametersForPartition(*verified_result_, partition);
    if (!batch_params.ok()) {
      return fault_handler_.SignalFault(batch_params.status().message(), *this, context).ToStatus();
    }

    for (int i = 0; i < batch_params->padding_count; ++i) {
      ShuffledMessage padding;
      *padding.mutable_metric_epoch_key() = partition;
      padding.set_run_id(verified_result_->run_context.run_id());
      padding.set_is_padding(true);

      context.IncrementCounter(kEncryptedOutputRecordCount);
      absl::Status emit_status = EmitEncrypted(context, partition, padding);
      if (!emit_status.ok()) {
        return fault_handler_.SignalFault(emit_status.message(), *this, context).ToStatus();
      }
    }
  }
  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler
