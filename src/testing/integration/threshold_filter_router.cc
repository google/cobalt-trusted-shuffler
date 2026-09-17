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

#include "src/testing/integration/threshold_filter_router.h"

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "containers/session.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

absl::StatusOr<KVCollection> RouteToThresholdFilter(
    const EncryptedBlob& verification_payload, absl::Span<const EncryptedBlob> partition_messages) {
  KVCollection inputs;
  inputs.reserve(1 + partition_messages.size());
  inputs.push_back(verification_payload.ToKV());

  for (const EncryptedBlob& msg : partition_messages) {
    inputs.push_back(msg.ToKV());
  }

  return inputs;
}

absl::StatusOr<KVCollection> RouteToThresholdFilterWithLateVerificationPayload(
    const EncryptedBlob& verification_payload, absl::Span<const EncryptedBlob> partition_messages) {
  KVCollection inputs;
  inputs.reserve(1 + partition_messages.size());
  for (const EncryptedBlob& msg : partition_messages) {
    inputs.push_back(msg.ToKV());
  }
  inputs.push_back(verification_payload.ToKV());
  return inputs;
}

}  // namespace cobalt::trusted_shuffler::testing::integration
