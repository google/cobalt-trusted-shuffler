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

#ifndef SRC_TESTING_INTEGRATION_SIMULATED_CONTEXT_H_
#define SRC_TESTING_INTEGRATION_SIMULATED_CONTEXT_H_

#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "containers/session.h"
#include "fcp/protos/confidentialcompute/confidential_transform.pb.h"
#include "src/containers/base/pipeline_nodes.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

// Session::Context implementation that captures everything a DoFn emits during
// replica execution.
//
// A container session always emits a (key, payload) pair, so this sink always
// keeps the key. Deciding whether the key survives past the enclave boundary is
// the host pipeline's job and happens in DoFnRunner.
class SimulatedSessionContext : public confidential_federated_compute::Session::Context {
 public:
  explicit SimulatedSessionContext(absl::Span<const Node> destinations)
      : destinations_(destinations.begin(), destinations.end()) {}
  ~SimulatedSessionContext() override = default;

  // CHECK-FAILs if called; raw ReadResponse emission is not used in the pipeline.
  bool Emit(fcp::confidentialcompute::ReadResponse read_response) override;

  bool EmitUnencrypted(confidential_federated_compute::Session::KV kv) override;

  bool EmitEncrypted(int reencryption_key_index,
                     confidential_federated_compute::Session::KV kv) override;

  // CHECK-FAILs if called; EmitReleasable is not used in the currently implemented pipeline stages.
  bool EmitReleasable(int reencryption_key_index, confidential_federated_compute::Session::KV kv,
                      std::optional<absl::string_view> src_state, absl::string_view dst_state,
                      std::string& release_token) override;

  confidential_federated_compute::Counters& GetCounters() override { return counters_; }

  const confidential_federated_compute::Counters& GetCounters() const { return counters_; }

  // Returns all encrypted records emitted by the session.
  const std::vector<EncryptedKVBlob>& Encrypted() const { return encrypted_; }

  // Returns all unencrypted records emitted by the session.
  const std::vector<UnencryptedKVBlob>& Unencrypted() const { return unencrypted_; }

 private:
  std::vector<Node> destinations_;
  confidential_federated_compute::Counters counters_;
  std::vector<EncryptedKVBlob> encrypted_;
  std::vector<UnencryptedKVBlob> unencrypted_;
};

}  // namespace cobalt::trusted_shuffler::testing::integration

#endif  // SRC_TESTING_INTEGRATION_SIMULATED_CONTEXT_H_
