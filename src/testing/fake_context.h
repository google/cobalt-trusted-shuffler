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

  bool EmitReleasable(int reencryption_key_index, confidential_federated_compute::Session::KV kv,
                      std::optional<absl::string_view> src_state,
                      absl::string_view dst_state,
                      std::string& release_token) override {
    releasable_.push_back({reencryption_key_index, std::move(kv),
                           src_state ? std::make_optional(std::string(*src_state)) : std::nullopt,
                           std::string(dst_state)});
    release_token = "fake_release_token";
    return true;
  }

  bool EmitError(absl::Status status) override {
    errors_.push_back(std::move(status));
    return true;
  }

  struct ReleasableInfo {
    int reencryption_key_index;
    confidential_federated_compute::Session::KV kv;
    std::optional<std::string> src_state;
    std::string dst_state;
  };

  std::vector<ReleasableInfo> Releasable() const { return releasable_; }
  std::vector<absl::Status> Errors() const { return errors_; }

 private:
  std::vector<ReleasableInfo> releasable_;
  std::vector<absl::Status> errors_;
  std::vector<fcp::confidentialcompute::ReadResponse> read_responses_;
  std::vector<confidential_federated_compute::Session::KV> unencrypted_;
  std::vector<std::pair<int, confidential_federated_compute::Session::KV>> encrypted_;
};

}  // namespace cobalt::trusted_shuffler::testing

#endif  // SRC_TESTING_FAKE_CONTEXT_H_