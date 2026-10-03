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

#include "src/testing/integration/simulated_context.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "containers/session.h"
#include "fcp/protos/confidentialcompute/confidential_transform.pb.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

bool SimulatedSessionContext::Emit(fcp::confidentialcompute::ReadResponse /*read_response*/) {
  CHECK(false) << "SimulatedSessionContext::Emit is not supported in integration tests.";
  return false;
}

bool SimulatedSessionContext::EmitUnencrypted(confidential_federated_compute::Session::KV kv) {
  unencrypted_.push_back(UnencryptedKVBlob(std::move(kv.key), std::move(kv.data)));
  return true;
}

bool SimulatedSessionContext::EmitEncrypted(const int reencryption_key_index,
                                            confidential_federated_compute::Session::KV kv) {
  CHECK_GE(reencryption_key_index, 0);
  CHECK_LT(static_cast<size_t>(reencryption_key_index), destinations_.size());
  encrypted_.push_back(EncryptedKVBlob(destinations_[reencryption_key_index], std::move(kv.key),
                                       std::move(kv.data)));
  return true;
}

bool SimulatedSessionContext::EmitReleasable(const int /*reencryption_key_index*/,
                                             confidential_federated_compute::Session::KV /*kv*/,
                                             std::optional<absl::string_view> /*src_state*/,
                                             const absl::string_view /*dst_state*/,
                                             std::string& /*release_token*/) {
  CHECK(false) << "SimulatedSessionContext::EmitReleasable is not supported in integration tests.";
  return false;
}

}  // namespace cobalt::trusted_shuffler::testing::integration
