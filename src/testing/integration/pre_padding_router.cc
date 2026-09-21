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

#include "src/testing/integration/pre_padding_router.h"

#include "absl/status/statusor.h"
#include "containers/session.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

absl::StatusOr<KVCollection> RouteToPrePadding(const EncryptedBlob& verification_payload) {
  KVCollection inputs;
  inputs.reserve(1);
  inputs.push_back(verification_payload.ToKV());
  return inputs;
}

absl::StatusOr<KVCollection> RouteToPrePaddingWithEmptyInput(
    const EncryptedBlob& /*verification_payload*/) {
  KVCollection inputs;
  inputs.push_back(confidential_federated_compute::Session::KV(/*data=*/""));
  return inputs;
}

absl::StatusOr<KVCollection> RouteToPrePaddingWithUnexpectedRecord(
    const EncryptedBlob& verification_payload) {
  KVCollection inputs;
  inputs.push_back(verification_payload.ToKV());
  inputs.push_back(confidential_federated_compute::Session::KV(/*data=*/"unexpected_record"));
  return inputs;
}

}  // namespace cobalt::trusted_shuffler::testing::integration
