#include "src/containers/simple/simple_session.h"

#include <string>

#include "absl/status/statusor.h"
#include "containers/session.h"
#include "fcp/protos/confidentialcompute/confidential_transform.pb.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::confidential_federated_compute::Session;
using ::fcp::confidentialcompute::BlobMetadata;
using ::fcp::confidentialcompute::CommitRequest;
using ::fcp::confidentialcompute::CommitResponse;
using ::fcp::confidentialcompute::ConfigureRequest;
using ::fcp::confidentialcompute::ConfigureResponse;
using ::fcp::confidentialcompute::FinalizeRequest;
using ::fcp::confidentialcompute::FinalizeResponse;
using ::fcp::confidentialcompute::WriteFinishedResponse;
using ::fcp::confidentialcompute::WriteRequest;

}  // namespace

absl::StatusOr<ConfigureResponse> SimpleSession::Configure(ConfigureRequest request,
                                                           Context& context) {
  return ConfigureResponse();
}

absl::StatusOr<WriteFinishedResponse> SimpleSession::Write(WriteRequest request,
                                                           std::string unencrypted_data,
                                                           Context& context) {
  blobs_.push_back(std::move(unencrypted_data));
  return WriteFinishedResponse();
}

absl::StatusOr<CommitResponse> SimpleSession::Commit(CommitRequest request, Context& context) {
  // TODO(b/492583850): Figure out what the key index should be.
  int reencryption_key_index = 0;
  for (const std::string& blob : blobs_) {
    context.EmitEncrypted(reencryption_key_index, blob);
  }
  blobs_.clear();

  return CommitResponse();
}

absl::StatusOr<FinalizeResponse> SimpleSession::Finalize(FinalizeRequest request,
                                                         BlobMetadata input_metadata,
                                                         Context& context) {
  return absl::UnimplementedError("Not implemented");
}

}  // namespace cobalt::trusted_shuffler