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

#ifndef SRC_CONTAINERS_RUN_CONTEXT_INITIALIZER_RUN_CONTEXT_INITIALIZER_FN_H_
#define SRC_CONTAINERS_RUN_CONTEXT_INITIALIZER_RUN_CONTEXT_INITIALIZER_FN_H_

#include <cstddef>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "cc/crypto/signing_key.h"
#include "containers/fns/do_fn.h"
#include "google/protobuf/any.pb.h"
#include "src/proto/run_context.pb.h"

namespace cobalt::trusted_shuffler {

// A DoFn class for generating a SignedRunContext from a RunConfig.
class RunContextInitializerFn
    : public confidential_federated_compute::fns::DoFn {
 public:
  explicit RunContextInitializerFn(
      oak::crypto::SigningKeyHandle* signing_key_handle)
      : signing_key_handle_(signing_key_handle) {}

  ~RunContextInitializerFn() override = default;

  absl::Status InitializeReplica(google::protobuf::Any config,
                                 Context& context) override;

  absl::Status Do(KV input, Context& context) override;

  absl::Status FinalizeReplica(google::protobuf::Any config,
                               Context& context) override;

 private:
  oak::crypto::SigningKeyHandle* signing_key_handle_;
  std::optional<RunConfig> run_config_;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_RUN_CONTEXT_INITIALIZER_RUN_CONTEXT_INITIALIZER_FN_H_
