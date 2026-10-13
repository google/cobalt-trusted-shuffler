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

#include "src/containers/ingestion_verification/ingestion_verification_fn.h"

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "google/protobuf/any.pb.h"
#include "src/containers/base/counter_names.h"
#include "src/containers/base/integrity_fault.h"
#include "src/crypto/multiset_fingerprint.h"
#include "src/crypto/verify_signed_run_context.h"
#include "src/proto/common.pb.h"
#include "src/proto/config_constraints.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/sketch/hll_sketch.h"

namespace cobalt::trusted_shuffler {

IngestionVerificationFn::IngestionVerificationFn(
    TrustedShufflerConfigConstraints config_constraints)
    : config_constraints_(std::move(config_constraints)), fault_handler_([]() {
        IngestionVerificationHeader header;
        header.mutable_integrity_fault();
        return header;
      }()) {
  for (const auto& metric_threshold : config_constraints_.metric_thresholds()) {
    threshold_map_[{metric_threshold.project_id(), metric_threshold.metric_id()}] =
        metric_threshold.threshold();
  }
}

absl::Status IngestionVerificationFn::InitializeReplica(google::protobuf::Any config,
                                                        Context& context) {
  for (const auto& metric_threshold : config_constraints_.metric_thresholds()) {
    LOG(INFO) << "Config constraint: project_id=" << metric_threshold.project_id()
              << ", metric_id=" << metric_threshold.metric_id()
              << ", threshold=" << metric_threshold.threshold();
  }
  return absl::OkStatus();
}

absl::Status IngestionVerificationFn::OnConfig(
    const cobalt::trusted_shuffler::SignedRunContext& signed_run_context, DoContext& context) {
  context.IncrementCounter(kInputConfigCount);
  const absl::StatusOr<RunContext> verified_run_context =
      VerifySignedRunContext(signed_run_context);
  if (!verified_run_context.ok()) {
    context.IncrementCounter(kInputConfigInvalid);
    return fault_handler_.SignalFault(verified_run_context.status().message(), *this, context)
        .ToStatus();
  }
  run_id_ = verified_run_context->run_id();
  target_batch_size_ = verified_run_context->run_config().target_batch_size();
  multiset_fingerprint_.emplace(Stage::STAGE_INGESTION, /*is_partial=*/false, *verified_run_context,
                                MultisetFingerprint::kFingerprintSize);
  LOG(INFO) << "SignedRunContext received.";
  return absl::OkStatus();
}

absl::Status IngestionVerificationFn::OnRecord(const PartialIngestionReport& report,
                                               DoContext& context) {
  context.IncrementCounter(kInputRecordCount);
  if (fault_handler_.HasFault()) {
    return fault_handler_.SignalFault(*this, context).ToStatus();
  }

  // Merge the partial multiset fingerprint (validates run_id, stage_id, and partial status).
  const absl::Status fp_status = multiset_fingerprint_->Merge(report.partial_fingerprint());
  if (!fp_status.ok()) {
    return fault_handler_
        .SignalFault(absl::StrCat("Failed to merge partial fingerprint: ", fp_status.message()),
                     *this, context)
        .ToStatus();
  }

  // Collect and merge HyperLogLog sketches.
  for (const auto& metric_epoch_sketch : report.metric_epoch_sketches()) {
    const MetricEpochKey& key = metric_epoch_sketch.metric_epoch_key();
    const MetricEpochTuple tuple_key = {key.project_id(), key.metric_id(), key.epoch_index()};

    const auto it = partition_sketches_.find(tuple_key);
    if (it != partition_sketches_.end()) {
      const absl::Status merge_status =
          it->second.MergeFrom(metric_epoch_sketch.hyperloglog_sketch());
      if (!merge_status.ok()) {
        return fault_handler_
            .SignalFault(
                absl::StrCat("Failed to merge HllSketch for partition: ", merge_status.message()),
                *this, context)
            .ToStatus();
      }
    } else {
      absl::StatusOr<HllSketch> sketch =
          HllSketch::FromProto(metric_epoch_sketch.hyperloglog_sketch());
      if (!sketch.ok()) {
        return fault_handler_
            .SignalFault(absl::StrCat("Failed to deserialize HllSketch for partition: ",
                                      sketch.status().message()),
                         *this, context)
            .ToStatus();
      }
      partition_sketches_.emplace(tuple_key, std::move(*sketch));
      partition_keys_.emplace(tuple_key, key);
    }
  }

  return absl::OkStatus();
}

absl::Status IngestionVerificationFn::OnFinalize(google::protobuf::Any config, FnContext& context) {
  if (fault_handler_.HasFault()) {
    return fault_handler_.SignalFault(*this, context).ToStatus();
  }
  if (!HasConfig() || !multiset_fingerprint_.has_value()) {
    return fault_handler_
        .SignalFault("Configuration was not established before FinalizeReplica.", *this, context)
        .ToStatus();
  }

  const RunFingerprint raw_fingerprint = multiset_fingerprint_->Build();
  absl::flat_hash_map<MetricEpochTuple, const FingerprintEntry*> raw_entries_by_tuple;
  for (const FingerprintEntry& entry : raw_fingerprint.entries()) {
    const MetricEpochTuple tuple_key = {entry.metric_epoch_key().project_id(),
                                        entry.metric_epoch_key().metric_id(),
                                        entry.metric_epoch_key().epoch_index()};
    raw_entries_by_tuple[tuple_key] = &entry;
  }

  const int64_t max_partition_messages =
      static_cast<int64_t>(target_batch_size_) * static_cast<int64_t>(target_batch_size_);

  ThresholdReport threshold_report;
  threshold_report.set_run_id(run_id_);

  RunFingerprint canonical_fingerprint;
  canonical_fingerprint.set_run_id(run_id_);
  canonical_fingerprint.set_stage_id(Stage::STAGE_INGESTION);
  canonical_fingerprint.set_is_partial(false);

  // Evaluate thresholds and max batch size bounds for all partitions that have sketches.
  for (const auto& [tuple_key, sketch] : partition_sketches_) {
    const auto raw_entry_it = raw_entries_by_tuple.find(tuple_key);
    if (raw_entry_it == raw_entries_by_tuple.end() || raw_entry_it->second->message_count() <= 0) {
      return fault_handler_.SignalFault("Missing message count for partition.", *this, context)
          .ToStatus();
    }

    const int64_t unique_device_count = sketch.EstimateCardinality();
    const int32_t project_id = std::get<0>(tuple_key);
    const int32_t metric_id = std::get<1>(tuple_key);

    const auto threshold_it = threshold_map_.find({project_id, metric_id});
    if (threshold_it == threshold_map_.end() || unique_device_count < threshold_it->second) {
      continue;
    }

    if (raw_entry_it->second->message_count() > max_partition_messages) {
      context.IncrementCounter(kOversizedPartitionDropped);
      continue;
    }

    const MetricEpochKey& key = partition_keys_[tuple_key];
    *threshold_report.add_approved_partitions() = key;
    *canonical_fingerprint.add_entries() = *raw_entry_it->second;
  }

  IngestionVerificationHeader header;
  header.mutable_ingestion_result();

  IngestionVerificationPayload payload;
  IngestionVerificationResult* const result = payload.mutable_success();
  *result->mutable_threshold_report() = std::move(threshold_report);
  *result->mutable_canonical_fingerprint() = std::move(canonical_fingerprint);
  *result->mutable_signed_run_context() = *Config();

  context.IncrementCounter(kEncryptedOutputRecordCount);
  EmitEncrypted(context, header, payload).IgnoreError();

  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler
