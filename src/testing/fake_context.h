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