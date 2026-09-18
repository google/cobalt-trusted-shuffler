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
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "src/batching/batch_calculator.h"
#include "src/crypto/shuffle.h"
#include "src/crypto/verify_ingestion_verification_payload.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/proto/scatter_shuffle.pb.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler {

absl::Status ScatterShuffleFn::OnConfig(
    const IngestionVerificationPayload& ingestion_verification_payload, DoContext& context) {
  absl::StatusOr<VerifiedIngestionVerificationResult> verified_result =
      VerifyIngestionVerificationPayload(ingestion_verification_payload);
  if (!verified_result.ok()) {
    fault_reason_ = verified_result.status().message();
    return SignalIntegrityFault(context);
  }

  verified_result_ = *std::move(verified_result);
  return absl::OkStatus();
}

absl::Status ScatterShuffleFn::OnRecord(const ShuffledMessage& message, DoContext& context) {
  if (fault_reason_.has_value()) {
    return SignalIntegrityFault(context);
  }

  if (!verified_result_.has_value()) {
    fault_reason_ =
        "IntegrityFault: IngestionVerificationPayload configuration was not established before "
        "processing record.";
    return SignalIntegrityFault(context);
  }

  if (!message.has_metric_epoch_key()) {
    fault_reason_ = "IntegrityFault: Message missing required MetricEpochKey.";
    return SignalIntegrityFault(context);
  }

  const MetricEpochKey& metric_epoch_key = message.metric_epoch_key();

  // On first message record: compute batching parameters for this partition.
  if (!expected_metric_epoch_key_.has_value()) {
    absl::StatusOr<BatchParameters> batch_parameters =
        ComputeBatchParametersForPartition(*verified_result_, metric_epoch_key);
    if (!batch_parameters.ok()) {
      fault_reason_ = "IntegrityFault: Failed to compute batch parameters for partition.";
      return SignalIntegrityFault(context);
    }
    expected_metric_epoch_key_ = metric_epoch_key;
    batch_parameters_ = *std::move(batch_parameters);
  } else {
    // Validate key against expected_metric_epoch_key_.
    if (metric_epoch_key != *expected_metric_epoch_key_) {
      fault_reason_ = "IntegrityFault: MetricEpochKey mismatch in batch.";
      return SignalIntegrityFault(context);
    }
  }

  if (message.run_id() != verified_result_->run_context.run_id()) {
    fault_reason_ = "IntegrityFault: Message run_id mismatch.";
    return SignalIntegrityFault(context);
  }

  if (message.shuffle_count() != 0) {
    fault_reason_ = "IntegrityFault: Message shuffle_count must be 0.";
    return SignalIntegrityFault(context);
  }

  // Enforce batch size bound.
  if (messages_.size() >= static_cast<size_t>(batch_parameters_->target_batch_size)) {
    fault_reason_ = "IntegrityFault: Received more messages than expected batch size.";
    return SignalIntegrityFault(context);
  }

  messages_.push_back(message);
  return absl::OkStatus();
}

absl::Status ScatterShuffleFn::SignalIntegrityFault(FnContext& context) {
  const absl::string_view reason = fault_reason_.has_value()
                                       ? absl::string_view(*fault_reason_)
                                       : "IntegrityFault: Unspecified integrity fault.";

  if (!fault_emitted_) {
    fault_emitted_ = true;

    ScatterShuffleHeader header;
    header.mutable_integrity_fault();
    google::protobuf::Any key;
    key.PackFrom(header);

    IntegrityFault fault;
    context.EmitUnencrypted(KV(std::move(key), fault.SerializeAsString()));
  }

  return absl::FailedPreconditionError(reason);
}

absl::Status ScatterShuffleFn::FinalizeReplica(google::protobuf::Any config, FnContext& context) {
  if (!HasConfig() || !verified_result_.has_value()) {
    fault_reason_ = "IntegrityFault: Configuration was not established before FinalizeReplica.";
    return SignalIntegrityFault(context);
  }

  if (fault_reason_.has_value()) {
    return SignalIntegrityFault(context);
  }

  if (!batch_parameters_.has_value()) {
    fault_reason_ = "IntegrityFault: Batch parameters were not established (no records received).";
    return SignalIntegrityFault(context);
  }

  const int32_t target_batch_size = batch_parameters_->target_batch_size;
  if (static_cast<int32_t>(messages_.size()) != target_batch_size) {
    fault_reason_ = "IntegrityFault: Batch size mismatch.";
    return SignalIntegrityFault(context);
  }

  const absl::Status shuffle_status = SecureShuffle(messages_);
  if (!shuffle_status.ok()) {
    fault_reason_ = "IntegrityFault: Cryptographic shuffle failed.";
    return SignalIntegrityFault(context);
  }

  const int32_t batch_num = batch_parameters_->batch_num;
  if (batch_num <= 0) {
    fault_reason_ = "IntegrityFault: Invalid batch_num.";
    return SignalIntegrityFault(context);
  }
  for (size_t new_position = 0; new_position < messages_.size(); ++new_position) {
    const int32_t group_id = static_cast<int32_t>(new_position % batch_num);
    ShuffledMessage& message = messages_[new_position];
    message.set_group_id(group_id);
    message.set_shuffle_count(message.shuffle_count() + 1);

    ScatterShuffleHeader header;
    header.set_group_id(group_id);
    google::protobuf::Any key;
    key.PackFrom(header);

    // Note: kReencryptionKeyIndex (0) is the container's local output node index
    // for the downstream gather shuffle stage.
    context.EmitEncrypted(kReencryptionKeyIndex, KV(std::move(key), message.SerializeAsString()));
  }

  messages_.clear();
  expected_metric_epoch_key_.reset();
  batch_parameters_.reset();
  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler
