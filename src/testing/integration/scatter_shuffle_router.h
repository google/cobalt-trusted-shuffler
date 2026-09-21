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

#ifndef SRC_TESTING_INTEGRATION_SCATTER_SHUFFLE_ROUTER_H_
#define SRC_TESTING_INTEGRATION_SCATTER_SHUFFLE_ROUTER_H_

#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

// Groups real (ThresholdFilter) and padding (PrePadding) messages by their
// MetricEpochKey and divides them into batches of `target_batch_size`.
// Each batch gets prepended with the `verification_payload` as Record 0
// for execution on a ScatterShuffle replica.
absl::StatusOr<std::vector<KVCollection>> RouteToScatterShuffle(
    const EncryptedBlob& verification_payload, int32_t target_batch_size,
    absl::Span<const EncryptedKVBlob> threshold_filter_messages,
    absl::Span<const EncryptedKVBlob> pre_padding_messages);

absl::StatusOr<std::vector<KVCollection>> RouteUnderpaddedBatchToScatterShuffle(
    const EncryptedBlob& verification_payload, int32_t target_batch_size,
    absl::Span<const EncryptedKVBlob> threshold_filter_messages,
    absl::Span<const EncryptedKVBlob> pre_padding_messages);

absl::StatusOr<std::vector<KVCollection>> RouteToScatterShuffleWithLateVerificationPayload(
    const EncryptedBlob& verification_payload, int32_t target_batch_size,
    absl::Span<const EncryptedKVBlob> threshold_filter_messages,
    absl::Span<const EncryptedKVBlob> pre_padding_messages);

absl::StatusOr<std::vector<KVCollection>> RouteMixedPartitionBatchToScatterShuffle(
    const EncryptedBlob& verification_payload, int32_t target_batch_size,
    absl::Span<const EncryptedKVBlob> threshold_filter_messages,
    absl::Span<const EncryptedKVBlob> pre_padding_messages);

}  // namespace cobalt::trusted_shuffler::testing::integration

#endif  // SRC_TESTING_INTEGRATION_SCATTER_SHUFFLE_ROUTER_H_
