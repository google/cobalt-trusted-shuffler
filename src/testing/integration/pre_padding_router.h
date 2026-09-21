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

#ifndef SRC_TESTING_INTEGRATION_PRE_PADDING_ROUTER_H_
#define SRC_TESTING_INTEGRATION_PRE_PADDING_ROUTER_H_

#include "absl/status/statusor.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

// Prepends IngestionVerificationPayload as Record 0 for a PrePadding session.
// PrePadding takes no other records.
absl::StatusOr<KVCollection> RouteToPrePadding(const EncryptedBlob& verification_payload);

absl::StatusOr<KVCollection> RouteToPrePaddingWithEmptyInput(
    const EncryptedBlob& verification_payload);

absl::StatusOr<KVCollection> RouteToPrePaddingWithUnexpectedRecord(
    const EncryptedBlob& verification_payload);

}  // namespace cobalt::trusted_shuffler::testing::integration

#endif  // SRC_TESTING_INTEGRATION_PRE_PADDING_ROUTER_H_
