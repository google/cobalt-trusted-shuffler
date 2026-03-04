#ifndef SRC_CONTAINERS_SIMPLE_SIMPLE_TRANSFORM_H_
#define SRC_CONTAINERS_SIMPLE_SIMPLE_TRANSFORM_H_

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "containers/confidential_transform_server_base.h"
#include "containers/session.h"
#include "fcp/protos/confidentialcompute/confidential_transform.pb.h"
#include "google/protobuf/struct.pb.h"

namespace cobalt::trusted_shuffler {

// SimpleTransform implements the ConfidentialTransformBase API and provides a
// SimpleSession which implements the identity function. (See simple_session.h
// for details.)
class SimpleTransform : public confidential_federated_compute::ConfidentialTransformBase {
 public:
  SimpleTransform(std::unique_ptr<::oak::crypto::SigningKeyHandle> signing_key_handle,
                  std::unique_ptr<::oak::crypto::EncryptionKeyHandle> encryption_key_handle);

 protected:
  absl::StatusOr<google::protobuf::Struct> StreamInitializeTransform(
      const fcp::confidentialcompute::InitializeRequest* request) override;

  absl::Status ReadWriteConfigurationRequest(
      const fcp::confidentialcompute::WriteConfigurationRequest& write_configuration) override;

  absl::StatusOr<std::unique_ptr<confidential_federated_compute::Session>> CreateSession() override;

  absl::StatusOr<std::string> GetKeyId(
      const fcp::confidentialcompute::BlobMetadata& metadata) override;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_SIMPLE_SIMPLE_TRANSFORM_H_