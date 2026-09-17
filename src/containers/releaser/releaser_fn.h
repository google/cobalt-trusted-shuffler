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

#ifndef SRC_CONTAINERS_RELEASER_RELEASER_FN_H_
#define SRC_CONTAINERS_RELEASER_RELEASER_FN_H_

#include <string>

#include "absl/status/status.h"
#include "src/containers/base/with_outputs.h"

namespace cobalt::trusted_shuffler {

// A DoFn class for decrypting messages. It unconditionally decrypts all
// messages it receives.
class ReleaserFn
    : public TrustedDoFn::WithOutputs<Unencrypted<confidential_federated_compute::Session::KV>> {
 public:
  ReleaserFn() {}

  ~ReleaserFn() override = default;

  // Emits the input data unencrypted for release.
  absl::Status Do(KV input, DoContext& context) override;

 private:
  using FnContext = confidential_federated_compute::fns::Fn::FnContext;
  using DoContext = confidential_federated_compute::fns::DoFn::DoContext;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_RELEASER_RELEASER_FN_H_
