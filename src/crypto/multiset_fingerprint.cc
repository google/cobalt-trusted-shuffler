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

#include "src/crypto/multiset_fingerprint.h"

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/run_context.pb.h"

namespace cobalt::trusted_shuffler {
namespace {

using MetricEpochTuple = std::tuple<int32_t, int32_t, int64_t>;

MetricEpochTuple MetricEpochKeyToTuple(const MetricEpochKey& key) {
  return {key.project_id(), key.metric_id(), key.epoch_index()};
}

MetricEpochKey TupleToMetricEpochKey(const MetricEpochTuple& t) {
  MetricEpochKey key;
  key.set_project_id(std::get<0>(t));
  key.set_metric_id(std::get<1>(t));
  key.set_epoch_index(std::get<2>(t));
  return key;
}

}  // namespace

MultisetFingerprint::MultisetFingerprint(Stage stage_id, bool is_partial,
                                         absl::string_view run_id)
    : stage_id_(stage_id), is_partial_(is_partial), run_id_(run_id) {}

MultisetFingerprint::MultisetFingerprint(Stage stage_id, bool is_partial,
                                         const RunContext& run_context)
    : stage_id_(stage_id), is_partial_(is_partial), run_id_(run_context.run_id()) {}

void MultisetFingerprint::AddMessage(const ShuffledMessage& message) {
  if (message.is_padding()) {
    return;
  }
  partition_counts_[MetricEpochKeyToTuple(message.metric_epoch_key())]++;
}

absl::Status MultisetFingerprint::Merge(const RunFingerprint& partial_fingerprint) {
  if (is_partial_) {
    return absl::FailedPreconditionError(
        "Cannot merge fingerprints into a partial fingerprint; merging is only supported for building a complete stage fingerprint.");
  }
  if (partial_fingerprint.stage_id() != stage_id_) {
    return absl::InvalidArgumentError(
        absl::StrCat("Cannot merge fingerprints from different stages, attempting to merge stage ",
                     partial_fingerprint.stage_id(), " into stage ", stage_id_));
  }
  if (!partial_fingerprint.is_partial()) {
    return absl::InvalidArgumentError(
        "Only partial fingerprints produced by workers can be merged into a complete stage fingerprint.");
  }

  for (const FingerprintEntry& entry : partial_fingerprint.entries()) {
    partition_counts_[MetricEpochKeyToTuple(entry.metric_epoch_key())] +=
        entry.message_count();
  }

  return absl::OkStatus();
}

RunFingerprint MultisetFingerprint::Build() const {
  RunFingerprint fingerprint;
  fingerprint.set_run_id(run_id_);
  fingerprint.set_stage_id(stage_id_);
  fingerprint.set_is_partial(is_partial_);

  for (const auto& [tuple_key, count] : partition_counts_) {
    FingerprintEntry* entry = fingerprint.add_entries();
    *entry->mutable_metric_epoch_key() = TupleToMetricEpochKey(tuple_key);
    entry->set_message_count(count);
    // TODO(b/555363748): Implement actual cryptographic multiset fingerprinting algorithm (bytes
    // are currently empty).
  }

  return fingerprint;
}

bool MultisetFingerprint::VerifyComplete(const RunFingerprint& complete_fingerprint) const {
  if (run_id_ != complete_fingerprint.run_id()) {
    return false;
  }
  if (is_partial_) {
    return false;
  }
  if (complete_fingerprint.is_partial()) {
    return false;
  }

  std::map<MetricEpochTuple, int64_t> input_counts;
  for (const FingerprintEntry& entry : complete_fingerprint.entries()) {
    input_counts[MetricEpochKeyToTuple(entry.metric_epoch_key())] +=
        entry.message_count();
  }

  return partition_counts_ == input_counts;
}

bool MultisetFingerprint::VerifyCompleteForMetricEpoch(
    const RunFingerprint& complete_fingerprint, const MetricEpochKey& metric_epoch_key) const {
  if (run_id_ != complete_fingerprint.run_id()) {
    return false;
  }
  if (is_partial_) {
    return false;
  }
  if (complete_fingerprint.is_partial()) {
    return false;
  }

  if (partition_counts_.size() > 1) {
    return false;
  }

  const MetricEpochTuple tuple_key = MetricEpochKeyToTuple(metric_epoch_key);
  if (partition_counts_.size() == 1 && partition_counts_.begin()->first != tuple_key) {
    return false;
  }

  const std::map<MetricEpochTuple, int64_t>::const_iterator it =
      partition_counts_.find(tuple_key);
  const int64_t expected_count = (it != partition_counts_.end()) ? it->second : 0;

  int64_t actual_count = 0;
  for (const FingerprintEntry& entry : complete_fingerprint.entries()) {
    if (MetricEpochKeyToTuple(entry.metric_epoch_key()) == tuple_key) {
      actual_count += entry.message_count();
    }
  }

  return expected_count == actual_count;
}

}  // namespace cobalt::trusted_shuffler
