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

#include "src/containers/gather_shuffle/gather_shuffle_fn.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "src/batching/batch_calculator.h"
#include "src/containers/base/counter_names.h"
#include "src/crypto/shuffle.h"
#include "src/crypto/verify_ingestion_verification_payload.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler {

absl::Status GatherShuffleFn::OnConfig(const IngestionVerificationPayload& config,
                                       DoContext& context) {
  context.IncrementCounter(kInputConfigCount);

  absl::StatusOr<VerifiedIngestionVerificationResult> verified_result =
      VerifyIngestionVerificationPayload(config);
  if (!verified_result.ok()) {
    context.IncrementCounter(kInputConfigInvalid);
    return fault_handler_.SignalFault(verified_result.status().message(), *this, context)
        .ToStatus();
  }

  verified_result_ = *std::move(verified_result);
  return absl::OkStatus();
}

absl::Status GatherShuffleFn::OnRecord(const ShuffledMessage& input, DoContext& context) {
  context.IncrementCounter(kInputRecordCount);

  if (fault_handler_.HasFault()) {
    context.IncrementCounter(kInputRecordDropped);
    return fault_handler_.SignalFault(*this, context).ToStatus();
  }

  if (!verified_result_.has_value()) {
    context.IncrementCounter(kInputRecordDropped);
    return fault_handler_
        .SignalFault("Configuration was not established before processing record.", *this, context)
        .ToStatus();
  }

  if (!input.has_metric_epoch_key()) {
    context.IncrementCounter(kInputRecordDropped);
    return fault_handler_.SignalFault("Message missing required MetricEpochKey.", *this, context)
        .ToStatus();
  }

  if (input.run_id() != verified_result_->run_context.run_id()) {
    context.IncrementCounter(kInputRecordDropped);
    return fault_handler_.SignalFault("Message run_id mismatch.", *this, context).ToStatus();
  }

  if (input.shuffle_count() != 1) {
    context.IncrementCounter(kInputRecordDropped);
    return fault_handler_.SignalFault("Message shuffle_count must be 1.", *this, context)
        .ToStatus();
  }

  if (!expected_metric_epoch_key_.has_value()) {
    expected_metric_epoch_key_ = input.metric_epoch_key();
    expected_group_id_ = input.group_id();

    absl::StatusOr<int32_t> expected_size = ComputeExpectedGatherGroupSizeForPartition(
        *verified_result_, *expected_metric_epoch_key_, *expected_group_id_);
    if (!expected_size.ok()) {
      context.IncrementCounter(kInputRecordDropped);
      return fault_handler_.SignalFault(expected_size.status().message(), *this, context)
          .ToStatus();
    }
    expected_group_size_ = *expected_size;
  } else {
    if (input.metric_epoch_key() != *expected_metric_epoch_key_) {
      context.IncrementCounter(kInputRecordDropped);
      return fault_handler_.SignalFault("MetricEpochKey mismatch in batch.", *this, context)
          .ToStatus();
    }
    if (input.group_id() != *expected_group_id_) {
      context.IncrementCounter(kInputRecordDropped);
      return fault_handler_.SignalFault("group_id mismatch in batch.", *this, context).ToStatus();
    }
  }

  if (messages_.size() >= static_cast<size_t>(*expected_group_size_)) {
    context.IncrementCounter(kInputRecordDropped);
    return fault_handler_
        .SignalFault("Received more messages than expected gather group size.", *this, context)
        .ToStatus();
  }

  messages_.push_back(input);
  return absl::OkStatus();
}

absl::Status GatherShuffleFn::FinalizeReplica(google::protobuf::Any config, FnContext& context) {
  if (fault_handler_.HasFault()) {
    return fault_handler_.SignalFault(*this, context).ToStatus();
  }

  if (!HasConfig() || !verified_result_.has_value()) {
    return fault_handler_
        .SignalFault("Configuration was not established before FinalizeReplica.", *this, context)
        .ToStatus();
  }

  if (!expected_group_size_.has_value()) {
    return fault_handler_
        .SignalFault("Group parameters were not established (no records received).", *this, context)
        .ToStatus();
  }

  if (static_cast<int32_t>(messages_.size()) != *expected_group_size_) {
    return fault_handler_.SignalFault("Gather group size mismatch.", *this, context).ToStatus();
  }

  absl::Status shuffle_status = SecureShuffle(messages_);
  if (!shuffle_status.ok()) {
    return fault_handler_.SignalFault("Cryptographic shuffle failed.", *this, context).ToStatus();
  }

  for (size_t i = 0; i < messages_.size(); ++i) {
    messages_[i].set_shuffle_count(2);
    context.IncrementCounter(kEncryptedOutputRecordCount);
    absl::Status emit_status = EmitEncrypted(context, messages_[i]);
    if (!emit_status.ok()) {
      return fault_handler_.SignalFault("Failed to emit encrypted message.", *this, context)
          .ToStatus();
    }
  }

  messages_.clear();
  expected_metric_epoch_key_.reset();
  expected_group_id_.reset();
  expected_group_size_.reset();
  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler
