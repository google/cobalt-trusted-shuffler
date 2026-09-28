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

#include "src/containers/scatter_shuffle/scatter_shuffle_fn.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/batching/batch_calculator.h"
#include "src/containers/base/counter_names.h"
#include "src/containers/base/integrity_fault.h"
#include "src/crypto/shuffle.h"
#include "src/crypto/verify_ingestion_verification_payload.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/proto/scatter_shuffle.pb.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler {

ScatterShuffleFn::ScatterShuffleFn()
    : fault_handler_([]() {
        ScatterShuffleHeader header;
        header.mutable_integrity_fault();
        return header;
      }()) {}

absl::Status ScatterShuffleFn::OnConfig(
    const IngestionVerificationPayload& ingestion_verification_payload, DoContext& context) {
  context.IncrementCounter(kInputConfigCount);
  absl::StatusOr<VerifiedIngestionVerificationResult> verified_result =
      VerifyIngestionVerificationPayload(ingestion_verification_payload);
  if (!verified_result.ok()) {
    context.IncrementCounter(kInputConfigInvalid);
    return fault_handler_.SignalFault(verified_result.status().message(), *this, context)
        .ToStatus();
  }

  verified_result_ = *std::move(verified_result);
  return absl::OkStatus();
}

absl::Status ScatterShuffleFn::OnRecord(const ShuffledMessage& message, DoContext& context) {
  context.IncrementCounter(kInputRecordCount);
  if (fault_handler_.HasFault()) {
    return fault_handler_.SignalFault(*this, context).ToStatus();
  }

  if (!verified_result_.has_value()) {
    return fault_handler_
        .SignalFault(
            "IngestionVerificationPayload configuration was not established before processing "
            "record.",
            *this, context)
        .ToStatus();
  }

  if (!message.has_metric_epoch_key()) {
    return fault_handler_.SignalFault("Message missing required MetricEpochKey.", *this, context)
        .ToStatus();
  }

  const MetricEpochKey& metric_epoch_key = message.metric_epoch_key();

  // On first message record: compute batching parameters for this partition.
  if (!expected_metric_epoch_key_.has_value()) {
    absl::StatusOr<BatchParameters> batch_parameters =
        ComputeBatchParametersForPartition(*verified_result_, metric_epoch_key);
    if (!batch_parameters.ok()) {
      return fault_handler_
          .SignalFault("Failed to compute batch parameters for partition.", *this, context)
          .ToStatus();
    }
    expected_metric_epoch_key_ = metric_epoch_key;
    batch_parameters_ = *std::move(batch_parameters);
  } else {
    // Validate key against expected_metric_epoch_key_.
    if (metric_epoch_key != *expected_metric_epoch_key_) {
      return fault_handler_.SignalFault("MetricEpochKey mismatch in batch.", *this, context)
          .ToStatus();
    }
  }

  if (message.run_id() != verified_result_->run_context.run_id()) {
    return fault_handler_.SignalFault("Message run_id mismatch.", *this, context).ToStatus();
  }

  if (message.shuffle_count() != 0) {
    return fault_handler_.SignalFault("Message shuffle_count must be 0.", *this, context)
        .ToStatus();
  }

  // Enforce batch size bound.
  if (messages_.size() >= static_cast<size_t>(batch_parameters_->actual_batch_size)) {
    return fault_handler_
        .SignalFault("Received more messages than expected batch size.", *this, context)
        .ToStatus();
  }

  messages_.push_back(message);
  return absl::OkStatus();
}

absl::Status ScatterShuffleFn::OnFinalize(google::protobuf::Any config, FnContext& context) {
  if (!HasConfig() || !verified_result_.has_value()) {
    return fault_handler_
        .SignalFault("Configuration was not established before FinalizeReplica.", *this, context)
        .ToStatus();
  }

  if (fault_handler_.HasFault()) {
    return fault_handler_.SignalFault(*this, context).ToStatus();
  }

  if (!batch_parameters_.has_value()) {
    return fault_handler_
        .SignalFault("Batch parameters were not established (no records received).", *this, context)
        .ToStatus();
  }

  const int32_t expected_batch_size = batch_parameters_->actual_batch_size;
  if (static_cast<int32_t>(messages_.size()) != expected_batch_size) {
    return fault_handler_.SignalFault("Batch size mismatch.", *this, context).ToStatus();
  }

  const absl::Status shuffle_status = SecureShuffle(messages_);
  if (!shuffle_status.ok()) {
    return fault_handler_.SignalFault("Cryptographic shuffle failed.", *this, context).ToStatus();
  }

  const int32_t batch_num = batch_parameters_->batch_num;
  if (batch_num <= 0) {
    return fault_handler_.SignalFault("Invalid batch_num.", *this, context).ToStatus();
  }
  for (size_t new_position = 0; new_position < messages_.size(); ++new_position) {
    const int32_t group_id =
        GatherGroupIdForPosition(static_cast<int64_t>(new_position), batch_num);
    ShuffledMessage& message = messages_[new_position];
    message.set_group_id(group_id);
    message.set_shuffle_count(message.shuffle_count() + 1);

    ScatterShuffleHeader header;
    ScatterGroupKey* const group_key = header.mutable_scatter_group_key();
    *group_key->mutable_metric_epoch_key() = *expected_metric_epoch_key_;
    group_key->set_group_id(group_id);

    context.IncrementCounter(kEncryptedOutputRecordCount);
    EmitEncrypted(context, header, message).IgnoreError();
  }

  messages_.clear();
  expected_metric_epoch_key_.reset();
  batch_parameters_.reset();
  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler
