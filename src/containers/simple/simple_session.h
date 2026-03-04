#ifndef SRC_CONTAINERS_SIMPLE_SESSION_H_
#define SRC_CONTAINERS_SIMPLE_SESSION_H_

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "containers/session.h"
#include "fcp/protos/confidentialcompute/confidential_transform.pb.h"

namespace cobalt::trusted_shuffler {

// SimpleSession collects the a list of all blobs written to it and then emits
// them encrypted in the same order in which they were received when Commit is
// called.
class SimpleSession : public confidential_federated_compute::Session {
 public:
  ~SimpleSession() override = default;

  absl::StatusOr<fcp::confidentialcompute::ConfigureResponse> Configure(
      fcp::confidentialcompute::ConfigureRequest request, Context& context) override;

  absl::StatusOr<fcp::confidentialcompute::WriteFinishedResponse> Write(
      fcp::confidentialcompute::WriteRequest request, std::string unencrypted_data,
      Context& context) override;

  absl::StatusOr<fcp::confidentialcompute::CommitResponse> Commit(
      fcp::confidentialcompute::CommitRequest request, Context& context) override;

  absl::StatusOr<fcp::confidentialcompute::FinalizeResponse> Finalize(
      fcp::confidentialcompute::FinalizeRequest request,
      fcp::confidentialcompute::BlobMetadata input_metadata, Context& context) override;

 private:
  std::vector<std::string> blobs_;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_SIMPLE_SESSION_H_