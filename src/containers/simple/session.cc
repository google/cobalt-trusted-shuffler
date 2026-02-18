#include "src/containers/simple/session.h"

#include <string>

#include "absl/status/statusor.h"
#include "containers/session.h"
#include "fcp/protos/confidentialcompute/confidential_transform.pb.h"

namespace cobalt::trusted_shuffler {
namespace {

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

absl::StatusOr<ConfigureResponse> Session::Configure(ConfigureRequest request, Context &context) {
  return absl::UnimplementedError("Not implemented");
}

absl::StatusOr<WriteFinishedResponse> Session::Write(WriteRequest request,
                                                     std::string unencrypted_data,
                                                     Context &context) {
  return absl::UnimplementedError("Not implemented");
}

absl::StatusOr<CommitResponse> Session::Commit(CommitRequest request, Context &context) {
  return absl::UnimplementedError("Not implemented");
}

absl::StatusOr<FinalizeResponse> Session::Finalize(FinalizeRequest request,
                                                   BlobMetadata input_metadata, Context &context) {
  return absl::UnimplementedError("Not implemented");
}

}  // namespace cobalt::trusted_shuffler