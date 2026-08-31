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

#ifndef SRC_CONTAINERS_INGESTION_VERIFICATION_INGESTION_VERIFICATION_TRANSFORM_H_
#define SRC_CONTAINERS_INGESTION_VERIFICATION_INGESTION_VERIFICATION_TRANSFORM_H_

#include <memory>
#include <utility>

#include "absl/status/statusor.h"
#include "cc/crypto/encryption_key.h"
#include "cc/crypto/signing_key.h"
#include "containers/fns/confidential_transform_server.h"
#include "containers/fns/fn_factory.h"
#include "google/protobuf/any.pb.h"
#include "src/proto/config_constraints.pb.h"

namespace cobalt::trusted_shuffler {

// Factory for creating IngestionVerificationFn instances.
class IngestionVerificationFnFactory : public confidential_federated_compute::fns::FnFactory {
 public:
  explicit IngestionVerificationFnFactory(TrustedShufflerConfigConstraints config_constraints)
      : config_constraints_(std::move(config_constraints)) {}

  absl::StatusOr<std::unique_ptr<confidential_federated_compute::fns::Fn>> CreateFn()
      const override;

  const TrustedShufflerConfigConstraints& ConfigConstraints() const { return config_constraints_; }

 private:
  TrustedShufflerConfigConstraints config_constraints_;
};

// Provides an IngestionVerificationFnFactory by unpacking and validating
// config_constraints.
absl::StatusOr<std::unique_ptr<confidential_federated_compute::fns::FnFactory>>
ProvideIngestionVerificationFnFactory(
    const google::protobuf::Any& configuration, const google::protobuf::Any& config_constraints,
    const confidential_federated_compute::fns::WriteConfigurationMap& write_configuration_map);

// Creates an IngestionVerificationTransform service.
std::unique_ptr<confidential_federated_compute::fns::FnConfidentialTransform>
CreateIngestionVerificationTransform(
    std::unique_ptr<oak::crypto::SigningKeyHandle> signing_key_handle,
    std::unique_ptr<oak::crypto::EncryptionKeyHandle> encryption_key_handle);

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_INGESTION_VERIFICATION_INGESTION_VERIFICATION_TRANSFORM_H_
