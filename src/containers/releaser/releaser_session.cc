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

#include "src/containers/releaser/releaser_session.h"

#include <optional>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "containers/session.h"
#include "fcp/protos/confidentialcompute/confidential_transform.pb.h"

namespace cobalt::trusted_shuffler {
namespace {

using confidential_federated_compute::Session;
using fcp::confidentialcompute::BlobMetadata;
using fcp::confidentialcompute::CommitRequest;
using fcp::confidentialcompute::CommitResponse;
using fcp::confidentialcompute::ConfigureRequest;
using fcp::confidentialcompute::ConfigureResponse;
using fcp::confidentialcompute::FinalizeRequest;
using fcp::confidentialcompute::FinalizeResponse;
using fcp::confidentialcompute::WriteFinishedResponse;
using fcp::confidentialcompute::WriteRequest;

constexpr absl::string_view kDstState = "stopped";

}  // namespace

absl::StatusOr<ConfigureResponse> ReleaserSession::Configure(
    const ConfigureRequest request, Context& context) {
  return ConfigureResponse();
}

absl::StatusOr<WriteFinishedResponse> ReleaserSession::Write(
    const WriteRequest request, std::string unencrypted_data,
    Context& context) {
  context.EmitReleasable(key_id_, KV(std::move(unencrypted_data)), std::nullopt,
                         kDstState, release_token_);
  return WriteFinishedResponse();
}

absl::StatusOr<CommitResponse> ReleaserSession::Commit(
    const CommitRequest request, Context& context) {
  return CommitResponse();
}

absl::StatusOr<FinalizeResponse> ReleaserSession::Finalize(
    const FinalizeRequest request, const BlobMetadata input_metadata,
    Context& context) {
  FinalizeResponse response;
  response.set_release_token(release_token_);

  return response;
}

}  // namespace cobalt::trusted_shuffler