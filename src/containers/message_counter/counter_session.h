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

#ifndef SRC_CONTAINERS_MESSAGE_COUNTER_COUNTER_SESSION_H_
#define SRC_CONTAINERS_MESSAGE_COUNTER_COUNTER_SESSION_H_

#include <string>

#include "absl/status/statusor.h"
#include "containers/session.h"
#include "fcp/protos/confidentialcompute/confidential_transform.pb.h"

namespace cobalt::trusted_shuffler {

// A session class for counting messages. It keeps track of the number of
// messages it receives and outputs the total count in the finalize step.
class CounterSession : public confidential_federated_compute::Session {
 public:
  explicit CounterSession(int key_id) : key_id_(key_id), count_(0) {}

  ~CounterSession() override = default;

  // Configures the Session with the counter_name.
  absl::StatusOr<fcp::confidentialcompute::ConfigureResponse> Configure(
      fcp::confidentialcompute::ConfigureRequest request, Context& context) override;

  // Counts the message and forwards it encrypted as a `MESSAGE` output.
  absl::StatusOr<fcp::confidentialcompute::WriteFinishedResponse> Write(
      fcp::confidentialcompute::WriteRequest request, std::string unencrypted_data,
      Context& context) override;

  // Commit is a NOOP.
  absl::StatusOr<fcp::confidentialcompute::CommitResponse> Commit(
      fcp::confidentialcompute::CommitRequest request, Context& context) override;

  // Emits the final count report as a `REPORT` output.
  absl::StatusOr<fcp::confidentialcompute::FinalizeResponse> Finalize(
      fcp::confidentialcompute::FinalizeRequest request,
      fcp::confidentialcompute::BlobMetadata input_metadata, Context& context) override;

 private:
  std::string counter_name_;
  int key_id_;
  int64_t count_;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_MESSAGE_COUNTER_COUNTER_SESSION_H_