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

#ifndef SRC_CONTAINERS_RELEASER_RELEASER_TRANSFORM_H_
#define SRC_CONTAINERS_RELEASER_RELEASER_TRANSFORM_H_

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "containers/confidential_transform_server_base.h"
#include "containers/session.h"
#include "fcp/protos/confidentialcompute/confidential_transform.pb.h"

namespace cobalt::trusted_shuffler {

// ReleaserTransform implements the ConfidentialTransformBase API and provides a
// ReleaserSession which releases messages it receives.
class ReleaserTransform
    : public confidential_federated_compute::ConfidentialTransformBase {
 public:
  ReleaserTransform(
      std::unique_ptr<oak::crypto::SigningKeyHandle> signing_key_handle,
      std::unique_ptr<oak::crypto::EncryptionKeyHandle> encryption_key_handle);

 protected:
  absl::Status StreamInitializeTransform(
      const google::protobuf::Any& configuration,
      const google::protobuf::Any& config_constraints) override;

  absl::Status ReadWriteConfigurationRequest(
      const fcp::confidentialcompute::WriteConfigurationRequest&
          write_configuration) override;

  absl::StatusOr<std::unique_ptr<confidential_federated_compute::Session>>
  CreateSession() override;

  absl::StatusOr<std::string> GetKeyId(
      const fcp::confidentialcompute::BlobMetadata& metadata) override;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_RELEASER_RELEASER_TRANSFORM_H_