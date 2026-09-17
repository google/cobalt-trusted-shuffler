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

#ifndef SRC_TESTING_INTEGRATION_DO_FN_RUNNER_H_
#define SRC_TESTING_INTEGRATION_DO_FN_RUNNER_H_

#include <functional>
#include <optional>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "containers/fns/do_fn.h"
#include "containers/session.h"
#include "google/protobuf/message.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

// Drives a confidential_federated_compute::fns::DoFn instance through its
// complete session replica lifecycle (InitializeReplica -> Do -> FinalizeReplica)
// and hands back what the host pipeline would see afterwards.
//
// If InitializeReplica, Do, or FinalizeReplica returns a non-OK status, the
// replica immediately aborts (skipping FinalizeReplica if Do fails) and
// discards all emitted outputs from that replica, matching the host pipeline's
// replica failure behavior. To drop a single invalid record while continuing
// the replica, a DoFn calls Context::EmitError and returns OK from Do.
//
// A stage declares its host-visible output type by choosing one of the two run
// methods below. That choice is fixed for the whole stage, so callers cannot
// inspect a key the host pipeline would have discarded.
class DoFnRunner {
 public:
  // Inputs for executing a single DoFn session replica.
  struct ReplicaInputs {
    std::optional<std::reference_wrapper<const google::protobuf::Message>> init_config =
        std::nullopt;
    absl::Span<const confidential_federated_compute::Session::KV> inputs = {};
    std::optional<std::reference_wrapper<const google::protobuf::Message>> finalize_config =
        std::nullopt;
  };

  // Executes the replica lifecycle for a stage that discards emitted keys,
  // leaving the host with payloads only.
  static absl::StatusOr<BlobStageOutputs> RunReplicaWithBlobOutput(
      confidential_federated_compute::fns::DoFn& do_fn, const ReplicaInputs& replica_inputs);

  // Executes the replica lifecycle for a stage that keeps emitted keys, leaving
  // the host a plaintext key to route on alongside each payload.
  static absl::StatusOr<KVBlobStageOutputs> RunReplicaWithKVBlobOutput(
      confidential_federated_compute::fns::DoFn& do_fn, const ReplicaInputs& replica_inputs);
};

}  // namespace cobalt::trusted_shuffler::testing::integration

#endif  // SRC_TESTING_INTEGRATION_DO_FN_RUNNER_H_
