#include "src/containers/simple/simple_transform.h"

#include <memory>

#include "absl/status/statusor.h"
#include "containers/session.h"
#include "fcp/protos/confidentialcompute/confidential_transform.pb.h"
#include "google/protobuf/struct.pb.h"
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

absl::StatusOr<google::protobuf::Struct> SimpleTransform::StreamInitializeTransform(
    const InitializeRequest* request) {
  return google::protobuf::Struct();
}

absl::Status SimpleTransform::ReadWriteConfigurationRequest(
    const WriteConfigurationRequest& write_configuration) {
  return absl::OkStatus();
}

absl::StatusOr<std::unique_ptr<Session>> SimpleTransform::CreateSession() {
  return std::make_unique<SimpleSession>();
}

absl::StatusOr<std::string> SimpleTransform::GetKeyId(const BlobMetadata& metadata) {
  return absl::UnimplementedError("Not implemented");
}

}  // namespace cobalt::trusted_shuffler