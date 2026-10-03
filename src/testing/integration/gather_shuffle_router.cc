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

#include "src/testing/integration/gather_shuffle_router.h"

#include <map>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "src/proto/common.pb.h"
#include "src/proto/scatter_shuffle.pb.h"

namespace cobalt::trusted_shuffler::testing::integration {

absl::StatusOr<std::vector<KVCollection>> RouteToGatherShuffle(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedKVBlob> scatter_shuffle_messages) {
  using GroupKey = std::tuple<int32_t, int32_t, int64_t, int32_t>;
  std::map<GroupKey, KVCollection> grouped_batches;

  for (const EncryptedKVBlob& msg : scatter_shuffle_messages) {
    ScatterShuffleHeader header;
    if (!msg.key().UnpackTo(&header)) {
      return absl::InternalError(
          "Failed to unpack ScatterShuffleHeader from ScatterShuffle output key.");
    }
    if (header.has_integrity_fault()) {
      return absl::FailedPreconditionError(
          "ScatterShuffle reported an integrity fault in ScatterShuffleHeader.");
    }
    if (!header.has_scatter_group_key() || !header.scatter_group_key().has_metric_epoch_key()) {
      return absl::InvalidArgumentError(
          "ScatterShuffleHeader is missing scatter_group_key or metric_epoch_key.");
    }

    const ScatterGroupKey& scatter_group = header.scatter_group_key();
    const MetricEpochKey& partition_key = scatter_group.metric_epoch_key();
    const GroupKey key{partition_key.project_id(), partition_key.metric_id(),
                       partition_key.epoch_index(), scatter_group.group_id()};
    KVCollection& group = grouped_batches[key];
    if (group.empty()) {
      group.push_back(verification_payload.ToKV());
    }
    group.push_back(msg.DropKey().ToKV());
  }

  std::vector<KVCollection> all_gather_groups;
  all_gather_groups.reserve(grouped_batches.size());
  for (auto& [_, group] : grouped_batches) {
    all_gather_groups.push_back(std::move(group));
  }
  return all_gather_groups;
}

absl::StatusOr<std::vector<KVCollection>> RouteToGatherShuffleWithEmptyInput(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedKVBlob> scatter_shuffle_messages) {
  absl::StatusOr<std::vector<KVCollection>> batches =
      RouteToGatherShuffle(verification_payload, scatter_shuffle_messages);
  if (!batches.ok() || batches->empty()) {
    return batches;
  }

  batches->front().clear();
  return batches;
}

absl::StatusOr<std::vector<KVCollection>> RouteToGatherShuffleWithLateVerificationPayload(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedKVBlob> scatter_shuffle_messages) {
  absl::StatusOr<std::vector<KVCollection>> batches =
      RouteToGatherShuffle(verification_payload, scatter_shuffle_messages);
  if (!batches.ok() || batches->empty()) {
    return batches;
  }

  if (batches->front().size() > 1) {
    RoutedKV vp = std::move(batches->front().front());
    batches->front().erase(batches->front().begin());
    batches->front().push_back(std::move(vp));
  }
  return batches;
}

absl::StatusOr<std::vector<KVCollection>> RouteToGatherShuffleWithUnexpectedRecord(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedKVBlob> scatter_shuffle_messages) {
  absl::StatusOr<std::vector<KVCollection>> batches =
      RouteToGatherShuffle(verification_payload, scatter_shuffle_messages);
  if (!batches.ok() || batches->empty()) {
    return batches;
  }

  if (batches->front().size() > 1) {
    batches->front().insert(batches->front().begin() + 1, batches->front()[1]);
  }
  return batches;
}

absl::StatusOr<std::vector<KVCollection>> RouteMixedGroupToGatherShuffle(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedKVBlob> scatter_shuffle_messages) {
  absl::StatusOr<std::vector<KVCollection>> batches =
      RouteToGatherShuffle(verification_payload, scatter_shuffle_messages);
  if (!batches.ok() || batches->size() < 2) {
    return batches;
  }

  batches->front().push_back(batches->back().back());
  return batches;
}

}  // namespace cobalt::trusted_shuffler::testing::integration
