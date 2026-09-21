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

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/batching/batch_calculator.h"
#include "src/containers/base/counter_names.h"

namespace cobalt::trusted_shuffler {

absl::Status PrePaddingFn::SignalIntegrityFault(absl::string_view reason, FnContext& context) {
  has_integrity_fault_ = true;
  std::string formatted_message = std::string(reason);
  if (!absl::StartsWith(formatted_message, "IntegrityFault: ")) {
    formatted_message = absl::StrCat("IntegrityFault: ", reason);
  }

  if (!fault_emitted_) {
    fault_emitted_ = true;
    LOG(ERROR) << formatted_message;
    context.IncrementCounter(kUnencryptedOutputRecordCount);
    EmitUnencrypted(context, IntegrityFault()).IgnoreError();
  }

  return absl::FailedPreconditionError(formatted_message);
}

absl::Status PrePaddingFn::OnConfig(const IngestionVerificationPayload& config,
                                    DoContext& context) {
  context.IncrementCounter(kInputConfigCount);
  absl::StatusOr<VerifiedIngestionVerificationResult> result =
      VerifyIngestionVerificationPayload(config);
  if (!result.ok()) {
    context.IncrementCounter(kInputConfigInvalid);
    return SignalIntegrityFault(result.status().message(), context);
  }
  verified_result_ = *std::move(result);
  return absl::OkStatus();
}

absl::Status PrePaddingFn::OnRecord(const ShuffledMessage& /*input*/, DoContext& context) {
  context.IncrementCounter(kInputRecordCount);
  context.IncrementCounter(kInputRecordDropped);
  return SignalIntegrityFault("This session is config-only and does not accept data records.",
                              context);
}

absl::Status PrePaddingFn::FinalizeReplica(google::protobuf::Any /*config*/, FnContext& context) {
  if (has_integrity_fault_ || !verified_result_.has_value()) {
    return SignalIntegrityFault(
        "PrePaddingFn::FinalizeReplica called before IngestionVerificationPayload configuration "
        "was established.",
        context);
  }

  for (const MetricEpochKey& partition : verified_result_->approved_partitions) {
    absl::StatusOr<BatchParameters> batch_params =
        ComputeBatchParametersForPartition(*verified_result_, partition);
    if (!batch_params.ok()) {
      return SignalIntegrityFault(batch_params.status().message(), context);
    }

    for (int i = 0; i < batch_params->padding_count; ++i) {
      ShuffledMessage padding;
      *padding.mutable_metric_epoch_key() = partition;
      padding.set_run_id(verified_result_->run_context.run_id());
      padding.set_is_padding(true);

      context.IncrementCounter(kEncryptedOutputRecordCount);
      absl::Status emit_status = EmitEncrypted(context, partition, padding);
      if (!emit_status.ok()) {
        return SignalIntegrityFault(emit_status.message(), context);
      }
    }
  }
  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler
