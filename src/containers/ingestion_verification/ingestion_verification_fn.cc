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
#include "google/protobuf/any.pb.h"
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
    : config_constraints_(std::move(config_constraints)) {
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
    const cobalt::trusted_shuffler::SignedRunContext& signed_run_context, Context& context) {
  const absl::StatusOr<RunContext> verified_run_context =
      VerifySignedRunContext(signed_run_context);
  if (!verified_run_context.ok()) {
    return verified_run_context.status();
  }
  run_id_ = verified_run_context->run_id();
  multiset_fingerprint_.emplace(Stage::STAGE_INGESTION, /*is_partial=*/false, *verified_run_context,
                                MultisetFingerprint::kFingerprintSize);
  LOG(INFO) << "SignedRunContext received.";
  return absl::OkStatus();
}

absl::Status IngestionVerificationFn::OnRecord(KV record, Context& context) {
  if (has_integrity_fault_) {
    return absl::OkStatus();
  }

  PartialIngestionReport report;
  if (!report.ParseFromString(record.data)) {
    LOG(ERROR) << "Failed to parse PartialIngestionReport from record data.";
    has_integrity_fault_ = true;
    return absl::OkStatus();
  }

  // Merge the partial multiset fingerprint (validates run_id, stage_id, and partial status).
  const absl::Status fp_status = multiset_fingerprint_->Merge(report.partial_fingerprint());
  if (!fp_status.ok()) {
    LOG(ERROR) << "Failed to merge partial fingerprint: " << fp_status;
    has_integrity_fault_ = true;
    return absl::OkStatus();
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
        LOG(ERROR) << "Failed to merge HllSketch for partition: " << merge_status;
        has_integrity_fault_ = true;
        return absl::OkStatus();
      }
    } else {
      absl::StatusOr<HllSketch> sketch =
          HllSketch::FromProto(metric_epoch_sketch.hyperloglog_sketch());
      if (!sketch.ok()) {
        LOG(ERROR) << "Failed to deserialize HllSketch for partition: " << sketch.status();
        has_integrity_fault_ = true;
        return absl::OkStatus();
      }
      partition_sketches_.emplace(tuple_key, std::move(*sketch));
      partition_keys_.emplace(tuple_key, key);
    }
  }

  return absl::OkStatus();
}

void IngestionVerificationFn::EmitIntegrityFault(Context& context) {
  IngestionVerificationHeader header;
  header.mutable_integrity_fault();

  IngestionVerificationPayload payload;
  payload.mutable_integrity_fault();

  google::protobuf::Any header_any;
  header_any.PackFrom(header);

  context.EmitEncrypted(kReencryptionKeyIndex,
                        KV(std::move(header_any), payload.SerializeAsString()));
}

absl::Status IngestionVerificationFn::FinalizeReplica(google::protobuf::Any config,
                                                      Context& context) {
  if (!HasConfig() || has_integrity_fault_ || !multiset_fingerprint_.has_value()) {
    EmitIntegrityFault(context);
    return absl::OkStatus();
  }

  ThresholdReport threshold_report;
  threshold_report.set_run_id(run_id_);

  absl::flat_hash_set<MetricEpochTuple> approved_partition_set;

  // Evaluate thresholds for all partitions that have sketches.
  for (const auto& [tuple_key, sketch] : partition_sketches_) {
    const int64_t unique_device_count = sketch.EstimateCardinality();
    const int32_t project_id = std::get<0>(tuple_key);
    const int32_t metric_id = std::get<1>(tuple_key);

    const auto it = threshold_map_.find({project_id, metric_id});
    if (it != threshold_map_.end() && unique_device_count >= it->second) {
      const MetricEpochKey& key = partition_keys_[tuple_key];
      *threshold_report.add_approved_partitions() = key;
      approved_partition_set.insert(tuple_key);
    }
  }

  const RunFingerprint raw_fingerprint = multiset_fingerprint_->Build();
  RunFingerprint canonical_fingerprint;
  canonical_fingerprint.set_run_id(run_id_);
  canonical_fingerprint.set_stage_id(Stage::STAGE_INGESTION);
  canonical_fingerprint.set_is_partial(false);

  // Copy fingerprint entries for approved partitions from the raw fingerprint.
  absl::flat_hash_set<MetricEpochTuple> added_entries;
  for (const auto& entry : raw_fingerprint.entries()) {
    const MetricEpochTuple tuple_key = {entry.metric_epoch_key().project_id(),
                                        entry.metric_epoch_key().metric_id(),
                                        entry.metric_epoch_key().epoch_index()};
    if (approved_partition_set.contains(tuple_key)) {
      *canonical_fingerprint.add_entries() = entry;
      added_entries.insert(tuple_key);
    }
  }

  // Create new entries for approved partitions that were not in the raw fingerprint.
  for (const auto& tuple_key : approved_partition_set) {
    if (!added_entries.contains(tuple_key)) {
      FingerprintEntry* const entry = canonical_fingerprint.add_entries();
      *entry->mutable_metric_epoch_key() = partition_keys_[tuple_key];
      entry->set_message_count(partition_sketches_.at(tuple_key).EstimateCardinality());
    }
  }

  IngestionVerificationHeader header;
  header.mutable_ingestion_result();

  IngestionVerificationPayload payload;
  IngestionVerificationResult* const result = payload.mutable_success();
  *result->mutable_threshold_report() = std::move(threshold_report);
  *result->mutable_canonical_fingerprint() = std::move(canonical_fingerprint);
  *result->mutable_signed_run_context() = *Config();

  google::protobuf::Any header_any;
  header_any.PackFrom(header);

  context.EmitEncrypted(kReencryptionKeyIndex,
                        KV(std::move(header_any), payload.SerializeAsString()));

  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler
