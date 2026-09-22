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

#ifndef SRC_TESTING_INTEGRATION_GATHER_SHUFFLE_ROUTER_H_
#define SRC_TESTING_INTEGRATION_GATHER_SHUFFLE_ROUTER_H_

#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

// Groups scattered messages by their MetricEpochKey and group_id from
// ScatterShuffleHeader, strips the ScatterShuffleHeader key, and prepends the
// IngestionVerificationPayload as Record 0 for execution on a GatherShuffle
// replica.
absl::StatusOr<std::vector<KVCollection>> RouteToGatherShuffle(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedKVBlob> scatter_shuffle_messages);

// Fault-injection router that sends an empty input stream to GatherShuffle.
absl::StatusOr<std::vector<KVCollection>> RouteToGatherShuffleWithEmptyInput(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedKVBlob> scatter_shuffle_messages);

// Fault-injection router that places scattered messages before the
// IngestionVerificationPayload instead of as Record 0.
absl::StatusOr<std::vector<KVCollection>> RouteToGatherShuffleWithLateVerificationPayload(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedKVBlob> scatter_shuffle_messages);

// Fault-injection router that duplicates a scattered message so the gather
// group size exceeds expected_group_size.
absl::StatusOr<std::vector<KVCollection>> RouteToGatherShuffleWithUnexpectedRecord(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedKVBlob> scatter_shuffle_messages);

// Fault-injection router that mixes messages from two different gather groups
// into a single GatherShuffle replica.
absl::StatusOr<std::vector<KVCollection>> RouteMixedGroupToGatherShuffle(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedKVBlob> scatter_shuffle_messages);

}  // namespace cobalt::trusted_shuffler::testing::integration

#endif  // SRC_TESTING_INTEGRATION_GATHER_SHUFFLE_ROUTER_H_
