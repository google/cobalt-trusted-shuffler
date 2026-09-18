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

#include "containers/fns/do_fn.h"
#include "containers/fns/fn.h"
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
                      std::optional<absl::string_view> src_state, absl::string_view dst_state,
                      std::string& release_token) override {
    releasable_.push_back({reencryption_key_index, std::move(kv),
                           src_state ? std::make_optional(std::string(*src_state)) : std::nullopt,
                           std::string(dst_state)});
    release_token = "fake_release_token";
    return true;
  }

  confidential_federated_compute::Counters& GetCounters() override { return counters_; }

  const confidential_federated_compute::Counters& GetCounters() const { return counters_; }

  bool EmitError(absl::Status status) {
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
  confidential_federated_compute::Counters counters_;
  std::vector<ReleasableInfo> releasable_;
  std::vector<absl::Status> errors_;
  std::vector<fcp::confidentialcompute::ReadResponse> read_responses_;
  std::vector<confidential_federated_compute::Session::KV> unencrypted_;
  std::vector<std::pair<int, confidential_federated_compute::Session::KV>> encrypted_;
};

// Fake FnContext wrapping a Session::Context for testing Fn lifecycle methods.
class FakeFnContext : public confidential_federated_compute::fns::Fn::FnContext {
 public:
  explicit FakeFnContext(confidential_federated_compute::Session::Context& session_context,
                         fcp::confidentialcompute::AssociatedMetadata metadata = {})
      : FnContext(session_context, std::move(metadata)) {}
};

// Fake DoContext wrapping a Session::Context for testing DoFn::Do.
class FakeDoContext : public confidential_federated_compute::fns::DoFn::DoContext {
 public:
  explicit FakeDoContext(confidential_federated_compute::Session::Context& session_context,
                         fcp::confidentialcompute::AssociatedMetadata metadata = {})
      : DoContext(session_context, std::move(metadata)) {}
};

// Combined fake context holding Session::Context, DoContext, and FnContext,
// providing implicit conversions to each for seamless testing of DoFn pipelines.
class FakeCombinedContext {
 public:
  FakeCombinedContext() = default;

  operator FakeContext&() { return session_context_; }
  operator FakeDoContext&() { return do_context_; }
  operator FakeFnContext&() { return fn_context_; }
  operator confidential_federated_compute::Session::Context&() { return session_context_; }
  operator confidential_federated_compute::fns::DoFn::DoContext&() { return do_context_; }
  operator confidential_federated_compute::fns::Fn::FnContext&() { return fn_context_; }

  std::vector<fcp::confidentialcompute::ReadResponse> ReadResponses() const {
    return session_context_.ReadResponses();
  }
  std::vector<confidential_federated_compute::Session::KV> Unencrypted() const {
    return session_context_.Unencrypted();
  }
  std::vector<std::pair<int, confidential_federated_compute::Session::KV>> Encrypted() const {
    return session_context_.Encrypted();
  }
  std::vector<FakeContext::ReleasableInfo> Releasable() const {
    return session_context_.Releasable();
  }
  std::vector<absl::Status> Errors() const { return session_context_.Errors(); }
  const confidential_federated_compute::Counters& GetCounters() const {
    return session_context_.GetCounters();
  }
  confidential_federated_compute::Counters& GetCounters() { return session_context_.GetCounters(); }

 private:
  FakeContext session_context_;
  FakeDoContext do_context_{session_context_};
  FakeFnContext fn_context_{session_context_};
};

}  // namespace cobalt::trusted_shuffler::testing

#endif  // SRC_TESTING_FAKE_CONTEXT_H_