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

#include "src/containers/simple/simple_transform.h"

#include <memory>

#include "absl/status/statusor.h"
#include "containers/blob_metadata.h"
#include "containers/session.h"
#include "fcp/protos/confidentialcompute/confidential_transform.pb.h"
#include "src/containers/simple/simple_session.h"

using ::confidential_federated_compute::ConfidentialTransformBase;
using ::confidential_federated_compute::Session;
using ::fcp::confidentialcompute::BlobMetadata;
using ::fcp::confidentialcompute::InitializeRequest;
using ::fcp::confidentialcompute::WriteConfigurationRequest;
using ::oak::crypto::EncryptionKeyHandle;
using ::oak::crypto::SigningKeyHandle;

namespace cobalt::trusted_shuffler {

SimpleTransform::SimpleTransform(std::unique_ptr<SigningKeyHandle> signing_key_handle,
                                 std::unique_ptr<EncryptionKeyHandle> encryption_key_handle)
    : ConfidentialTransformBase(std::move(signing_key_handle), std::move(encryption_key_handle)) {}

absl::Status SimpleTransform::StreamInitializeTransform(
    const ::google::protobuf::Any& configuration,
    const ::google::protobuf::Any& config_constraints) {
  return absl::OkStatus();
}

absl::Status SimpleTransform::ReadWriteConfigurationRequest(
    const WriteConfigurationRequest& write_configuration) {
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<Session>> SimpleTransform::CreateSession() {
  return std::make_unique<SimpleSession>();
}

absl::StatusOr<std::string> SimpleTransform::GetKeyId(const BlobMetadata& metadata) {
  return confidential_federated_compute::GetKeyIdFromMetadata(metadata);
}

}  // namespace cobalt::trusted_shuffler