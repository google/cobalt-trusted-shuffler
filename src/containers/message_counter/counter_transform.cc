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

#include "src/containers/message_counter/counter_transform.h"

#include <memory>
#include <utility>

#include "absl/status/statusor.h"
#include "containers/fns/confidential_transform_server.h"
#include "containers/fns/fn_factory.h"
#include "src/containers/message_counter/counter_fn.h"

namespace cobalt::trusted_shuffler {
namespace {

using confidential_federated_compute::fns::Fn;
using confidential_federated_compute::fns::FnConfidentialTransform;
using confidential_federated_compute::fns::FnFactory;
using confidential_federated_compute::fns::WriteConfigurationMap;

// Index of the key in the list of destination nodes. Only one destination node
// is expected for this transform.
constexpr int kKeyIndex = 0;

}  // namespace

absl::StatusOr<std::unique_ptr<Fn>> CounterFnFactory::CreateFn() const {
  return std::make_unique<CounterFn>(kKeyIndex);
}

std::unique_ptr<FnConfidentialTransform> CreateCounterTransform(
    std::unique_ptr<oak::crypto::SigningKeyHandle> signing_key_handle,
    std::unique_ptr<oak::crypto::EncryptionKeyHandle> encryption_key_handle) {
  return std::make_unique<FnConfidentialTransform>(
      std::move(signing_key_handle),
      [](const google::protobuf::Any& configuration,
         const google::protobuf::Any& config_constraints,
         const WriteConfigurationMap& write_configuration_map)
          -> absl::StatusOr<std::unique_ptr<FnFactory>> {
        return std::make_unique<CounterFnFactory>();
      },
      std::move(encryption_key_handle));
}

}  // namespace cobalt::trusted_shuffler