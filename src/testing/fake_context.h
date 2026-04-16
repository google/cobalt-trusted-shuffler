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

#ifndef SRC_TESTING_FAKE_CONTEXT_H_
#define SRC_TESTING_FAKE_CONTEXT_H_

#include <string>
#include <utility>
#include <vector>

#include "containers/session.h"
#include "fcp/protos/confidentialcompute/confidential_transform.pb.h"

namespace cobalt::trusted_shuffler::testing {

// Fake implementation of Session::Context that captures calls to Emit, 
// EmitUnencrypted, and EmitEncrypted and stores them for testing.
class FakeContext : public confidential_federated_compute::Session::Context {
 public:
  bool Emit(fcp::confidentialcompute::ReadResponse read_response) override {
    read_responses_.push_back(std::move(read_response));
    return true;
  }

  bool EmitUnencrypted(confidential_federated_compute::Session::KV kv) override {
    unencrypted_.push_back(std::move(kv));
    return true;
  }

  bool EmitEncrypted(int reencryption_key_index,
                     confidential_federated_compute::Session::KV kv) override {
    encrypted_.push_back({reencryption_key_index, std::move(kv)});
    return true;
  }

  std::vector<fcp::confidentialcompute::ReadResponse> ReadResponses() const {
    return read_responses_;
  }

  std::vector<confidential_federated_compute::Session::KV> Unencrypted() const {
    return unencrypted_;
  }

  std::vector<std::pair<int, confidential_federated_compute::Session::KV>> Encrypted() const {
    return encrypted_;
  }

 private:
  std::vector<fcp::confidentialcompute::ReadResponse> read_responses_;
  std::vector<confidential_federated_compute::Session::KV> unencrypted_;
  std::vector<std::pair<int, confidential_federated_compute::Session::KV>> encrypted_;
};

}  // namespace cobalt::trusted_shuffler::testing

#endif  // SRC_TESTING_FAKE_CONTEXT_H_