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

#include "src/testing/integration/scatter_shuffle_router.h"

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "src/proto/common.pb.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler::testing::integration {

absl::StatusOr<std::vector<KVCollection>> RouteToScatterShuffle(
    const EncryptedBlob& verification_payload, int32_t target_batch_size,
    absl::Span<const EncryptedKVBlob> threshold_filter_messages,
    absl::Span<const EncryptedKVBlob> pre_padding_messages) {
  if (target_batch_size <= 0) {
    return absl::InvalidArgumentError("target_batch_size must be positive.");
  }

  absl::flat_hash_map<MetricEpochKey, std::vector<confidential_federated_compute::Session::KV>>
      partition_to_records;

  auto process_messages = [&](absl::Span<const EncryptedKVBlob> messages) -> absl::Status {
    for (const EncryptedKVBlob& msg : messages) {
      MetricEpochKey key;
      if (!msg.key().UnpackTo(&key)) {
        return absl::InternalError("Failed to unpack MetricEpochKey from EncryptedKVBlob.");
      }
      partition_to_records[key].push_back(msg.ToKV());
    }
    return absl::OkStatus();
  };

  if (auto status = process_messages(threshold_filter_messages); !status.ok()) {
    return status;
  }
  if (auto status = process_messages(pre_padding_messages); !status.ok()) {
    return status;
  }

  std::vector<MetricEpochKey> keys;
  for (const auto& [key, _] : partition_to_records) {
    keys.push_back(key);
  }
  std::sort(keys.begin(), keys.end(), [](const MetricEpochKey& a, const MetricEpochKey& b) {
    return std::make_tuple(a.project_id(), a.metric_id(), a.epoch_index()) <
           std::make_tuple(b.project_id(), b.metric_id(), b.epoch_index());
  });

  std::vector<KVCollection> batches;

  for (const auto& key : keys) {
    const auto& records = partition_to_records[key];
    const int64_t total_records = static_cast<int64_t>(records.size());
    const int64_t batch_num = (total_records + target_batch_size - 1) / target_batch_size;
    if (total_records % batch_num != 0) {
      return absl::InvalidArgumentError(absl::StrCat("Partition record count (", total_records,
                                                     ") is not a multiple of batch_num (",
                                                     batch_num, ")."));
    }
    const int64_t actual_batch_size = total_records / batch_num;

    KVCollection current_batch;
    current_batch.push_back(verification_payload.ToKV());
    int64_t count = 0;

    for (const auto& record : records) {
      current_batch.push_back(record);
      count++;
      if (count == actual_batch_size) {
        batches.push_back(std::move(current_batch));
        current_batch.clear();
        current_batch.push_back(verification_payload.ToKV());
        count = 0;
      }
    }
  }

  return batches;
}

absl::StatusOr<std::vector<KVCollection>> RouteUnderpaddedBatchToScatterShuffle(
    const EncryptedBlob& verification_payload, int32_t target_batch_size,
    absl::Span<const EncryptedKVBlob> threshold_filter_messages,
    absl::Span<const EncryptedKVBlob> pre_padding_messages) {
  auto batches = RouteToScatterShuffle(verification_payload, target_batch_size,
                                       threshold_filter_messages, pre_padding_messages);
  if (!batches.ok() || batches->empty())
    return batches;

  if (batches->front().size() > 1) {
    batches->front().pop_back();
  }
  return batches;
}

absl::StatusOr<std::vector<KVCollection>> RouteToScatterShuffleWithLateVerificationPayload(
    const EncryptedBlob& verification_payload, int32_t target_batch_size,
    absl::Span<const EncryptedKVBlob> threshold_filter_messages,
    absl::Span<const EncryptedKVBlob> pre_padding_messages) {
  auto batches = RouteToScatterShuffle(verification_payload, target_batch_size,
                                       threshold_filter_messages, pre_padding_messages);
  if (!batches.ok() || batches->empty())
    return batches;

  if (batches->front().size() > 1) {
    confidential_federated_compute::Session::KV vp = std::move(batches->front().front());
    batches->front().erase(batches->front().begin());
    batches->front().push_back(std::move(vp));
  }
  return batches;
}

absl::StatusOr<std::vector<KVCollection>> RouteMixedPartitionBatchToScatterShuffle(
    const EncryptedBlob& verification_payload, int32_t target_batch_size,
    absl::Span<const EncryptedKVBlob> threshold_filter_messages,
    absl::Span<const EncryptedKVBlob> pre_padding_messages) {
  auto batches = RouteToScatterShuffle(verification_payload, target_batch_size,
                                       threshold_filter_messages, pre_padding_messages);
  if (!batches.ok() || batches->size() < 2)
    return batches;

  std::swap(batches->front().back(), batches->back().back());
  return batches;
}

}  // namespace cobalt::trusted_shuffler::testing::integration
