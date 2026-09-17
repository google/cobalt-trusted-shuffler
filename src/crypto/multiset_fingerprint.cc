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

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>

#include "absl/container/flat_hash_set.h"
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

// Computes HMAC-SHA256 over `data` using `key`. Returns a 32-byte digest.
std::string ComputeHmacSha256(const absl::string_view key, const absl::string_view data) {
  std::string digest(SHA256_DIGEST_LENGTH, '\0');
  unsigned int len = 0;
  const void* key_ptr = key.empty() ? "" : key.data();
  const uint8_t* data_ptr = data.empty() ? reinterpret_cast<const uint8_t*>("")
                                         : reinterpret_cast<const uint8_t*>(data.data());
  HMAC(EVP_sha256(), key_ptr, key.size(), data_ptr, data.size(),
       reinterpret_cast<uint8_t*>(digest.data()), &len);
  return digest;
}

// Adds `addend` to `accumulator` modulo 2^(8*fingerprint_size) using big-endian byte order.
void AddModulo2To256(const absl::string_view addend, std::size_t fingerprint_size,
                     std::string& accumulator) {
  if (addend.size() != fingerprint_size) {
    return;
  }
  if (accumulator.size() != fingerprint_size) {
    accumulator.assign(fingerprint_size, '\0');
  }

  uint8_t* acc_8 = reinterpret_cast<uint8_t*>(accumulator.data());
  const uint8_t* add_8 = reinterpret_cast<const uint8_t*>(addend.data());

  uint16_t carry = 0;
  for (std::size_t i = fingerprint_size; i > 0; --i) {
    carry += acc_8[i - 1] + add_8[i - 1];
    acc_8[i - 1] = static_cast<uint8_t>(carry & 0xFF);
    carry >>= 8;
  }
}

}  // namespace

MultisetFingerprint::MultisetFingerprint(Stage stage_id, bool is_partial,
                                         const RunContext& run_context,
                                         std::size_t fingerprint_size)
    : stage_id_(stage_id),
      is_partial_(is_partial),
      run_id_(run_context.run_id()),
      run_hash_key_(run_context.run_hash_key()),
      fingerprint_size_(fingerprint_size) {}

void MultisetFingerprint::AddMessage(const ShuffledMessage& message) {
  if (message.is_padding()) {
    return;
  }
  const MetricEpochTuple tuple = MetricEpochKeyToTuple(message.metric_epoch_key());
  PartitionValue& partition = partitions_[tuple];
  partition.count++;

  const std::string hmac = ComputeHmacSha256(run_hash_key_, message.customer_message());
  absl::string_view addend = hmac;
  if (addend.size() > fingerprint_size_) {
    addend = addend.substr(addend.size() - fingerprint_size_);
  }
  AddModulo2To256(addend, fingerprint_size_, partition.fingerprint);
}

absl::Status MultisetFingerprint::Merge(const RunFingerprint& partial_fingerprint) {
  if (is_partial_) {
    return absl::FailedPreconditionError(
        "Cannot merge fingerprints into a partial fingerprint; merging is only supported for building a complete stage fingerprint.");
  }
  if (partial_fingerprint.run_id() != run_id_) {
    return absl::InvalidArgumentError("Cannot merge fingerprints from different run IDs.");
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
    const MetricEpochTuple tuple = MetricEpochKeyToTuple(entry.metric_epoch_key());
    PartitionValue& partition = partitions_[tuple];
    partition.count += entry.message_count();
    if (!entry.fingerprint().empty()) {
      AddModulo2To256(entry.fingerprint(), fingerprint_size_, partition.fingerprint);
    }
  }

  return absl::OkStatus();
}

RunFingerprint MultisetFingerprint::Build() const {
  RunFingerprint fingerprint;
  fingerprint.set_run_id(run_id_);
  fingerprint.set_stage_id(stage_id_);
  fingerprint.set_is_partial(is_partial_);

  for (const auto& [tuple_key, partition] : partitions_) {
    FingerprintEntry* entry = fingerprint.add_entries();
    *entry->mutable_metric_epoch_key() = TupleToMetricEpochKey(tuple_key);
    entry->set_message_count(partition.count);
    entry->set_fingerprint(partition.fingerprint);
  }

  return fingerprint;
}

bool IsZeroFingerprint(absl::string_view fp, std::size_t fingerprint_size) {
  return fp.empty() || fp == std::string(fingerprint_size, '\0');
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

  absl::flat_hash_set<MetricEpochTuple> seen_keys;
  for (const FingerprintEntry& entry : complete_fingerprint.entries()) {
    const MetricEpochTuple tuple = MetricEpochKeyToTuple(entry.metric_epoch_key());
    if (!seen_keys.insert(tuple).second) {
      return false;
    }
    const auto it = partitions_.find(tuple);
    const int64_t actual_count = (it != partitions_.end()) ? it->second.count : 0;
    const absl::string_view actual_fp =
        (it != partitions_.end()) ? absl::string_view(it->second.fingerprint) : absl::string_view();

    if (actual_count != entry.message_count()) {
      return false;
    }
    if (actual_count == 0) {
      if (!IsZeroFingerprint(actual_fp, fingerprint_size_) ||
          !IsZeroFingerprint(entry.fingerprint(), fingerprint_size_)) {
        return false;
      }
    } else if (actual_fp != entry.fingerprint()) {
      return false;
    }
  }

  for (const auto& [tuple, partition] : partitions_) {
    if (!seen_keys.contains(tuple)) {
      return false;
    }
  }

  return true;
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

  if (partitions_.size() > 1) {
    return false;
  }

  const MetricEpochTuple tuple_key = MetricEpochKeyToTuple(metric_epoch_key);
  if (partitions_.size() == 1 && partitions_.begin()->first != tuple_key) {
    return false;
  }

  const auto it = partitions_.find(tuple_key);
  const int64_t expected_count = (it != partitions_.end()) ? it->second.count : 0;
  const absl::string_view expected_fp =
      (it != partitions_.end()) ? absl::string_view(it->second.fingerprint) : absl::string_view();

  const auto& entries = complete_fingerprint.entries();
  const auto matching_it =
      std::find_if(entries.begin(), entries.end(), [&](const FingerprintEntry& entry) {
        return MetricEpochKeyToTuple(entry.metric_epoch_key()) == tuple_key;
      });

  if (matching_it == entries.end()) {
    return expected_count == 0 && IsZeroFingerprint(expected_fp, fingerprint_size_);
  }

  // Ensure there are no duplicate entries for the partition.
  const auto duplicate_it =
      std::find_if(matching_it + 1, entries.end(), [&](const FingerprintEntry& entry) {
        return MetricEpochKeyToTuple(entry.metric_epoch_key()) == tuple_key;
      });
  if (duplicate_it != entries.end()) {
    return false;
  }

  if (expected_count == 0) {
    return matching_it->message_count() == 0 && IsZeroFingerprint(expected_fp, fingerprint_size_) &&
           IsZeroFingerprint(matching_it->fingerprint(), fingerprint_size_);
  }

  return matching_it->message_count() == expected_count &&
         matching_it->fingerprint() == expected_fp;
}

}  // namespace cobalt::trusted_shuffler
