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

#include "src/containers/message_counter/counter_session.h"

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "containers/session.h"
#include "fcp/protos/confidentialcompute/confidential_transform.pb.h"
#include "src/containers/message_counter/proto/count_report.pb.h"
#include "src/containers/message_counter/proto/counter_config.pb.h"
#include "src/containers/message_counter/proto/output_key.pb.h"

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

}  // namespace

absl::StatusOr<ConfigureResponse> CounterSession::Configure(ConfigureRequest request,
                                                            Context& context) {
  CounterConfig config;
  if (!request.configuration().UnpackTo(&config)) {
    return absl::InvalidArgumentError("Failed to unpack configuration as CounterConfig");
  }

  if (config.counter_name().empty()) {
    return absl::InvalidArgumentError("No counter_name specified in ConfigureRequest");
  }
  counter_name_ = config.counter_name();
  return ConfigureResponse();
}

absl::StatusOr<WriteFinishedResponse> CounterSession::Write(WriteRequest request,
                                                            std::string unencrypted_data,
                                                            Context& context) {
  count_++;

  OutputKey output_key;
  output_key.set_type(OutputKey::MESSAGE);

  google::protobuf::Any key;
  key.PackFrom(output_key);

  context.EmitEncrypted(key_id_, KV(std::move(key), std::move(unencrypted_data)));
  return WriteFinishedResponse();
}

absl::StatusOr<CommitResponse> CounterSession::Commit(CommitRequest request, Context& context) {
  return CommitResponse();
}

absl::StatusOr<FinalizeResponse> CounterSession::Finalize(FinalizeRequest request,
                                                          BlobMetadata input_metadata,
                                                          Context& context) {
  CountReport report;
  report.set_counter_name(counter_name_);
  report.set_count(count_);

  OutputKey report_key;
  report_key.set_type(OutputKey::REPORT);

  google::protobuf::Any key;
  key.PackFrom(report_key);

  context.EmitUnencrypted(KV(std::move(key), report.SerializeAsString()));

  return FinalizeResponse();
}

}  // namespace cobalt::trusted_shuffler