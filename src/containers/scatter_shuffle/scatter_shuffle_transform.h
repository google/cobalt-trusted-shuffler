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

#ifndef SRC_CONTAINERS_SCATTER_SHUFFLE_SCATTER_SHUFFLE_TRANSFORM_H_
#define SRC_CONTAINERS_SCATTER_SHUFFLE_SCATTER_SHUFFLE_TRANSFORM_H_

#include <memory>

#include "absl/status/statusor.h"
#include "containers/fns/confidential_transform_server.h"
#include "containers/fns/fn_factory.h"

namespace cobalt::trusted_shuffler {

class ScatterShuffleFnFactory : public confidential_federated_compute::fns::FnFactory {
 public:
  ScatterShuffleFnFactory() = default;

  absl::StatusOr<std::unique_ptr<confidential_federated_compute::fns::Fn>> CreateFn()
      const override;
};

// Creates a ScatterShuffleTransform service (instance of FnConfidentialTransform).
std::unique_ptr<confidential_federated_compute::fns::FnConfidentialTransform>
CreateScatterShuffleTransform(
    std::unique_ptr<oak::crypto::SigningKeyHandle> signing_key_handle,
    std::unique_ptr<oak::crypto::EncryptionKeyHandle> encryption_key_handle);

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_SCATTER_SHUFFLE_SCATTER_SHUFFLE_TRANSFORM_H_
