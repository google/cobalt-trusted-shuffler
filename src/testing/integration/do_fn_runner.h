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
#include "src/containers/base/pipeline_nodes.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

// Drives a confidential_federated_compute::fns::DoFn instance through its
// complete session replica lifecycle (InitializeReplica -> Do -> FinalizeReplica)
// and hands back what the host pipeline would see afterwards.
//
// If InitializeReplica, Do, or FinalizeReplica returns a non-OK status, or if an
// input record's data node does not match the session's declared input nodes
// (for FirstRecordConfigDoFn, record 0 must match kConfigSource and records 1..N
// must be in kRecordSources; for TrustedDoFn, all records must be in kSources),
// the replica immediately aborts (skipping FinalizeReplica if Do or input routing
// validation fails) and discards all emitted outputs from that replica,
// matching the host pipeline's replica failure behavior.
//
// A stage declares its host-visible output type by choosing one of the two run
// methods below. That choice is fixed for the whole stage, so callers cannot
// inspect a key the host pipeline would have discarded.
class DoFnRunner {
 public:
  // Inputs for executing a single DoFn session replica.
  struct ReplicaInputs {
    // Configuration unpacked during InitializeReplica, if any.
    std::optional<std::reference_wrapper<const google::protobuf::Message>> init_config =
        std::nullopt;

    // Input records streamed to Do in order.
    absl::Span<const RoutedKV> inputs = {};

    // Configuration unpacked during FinalizeReplica, if any.
    std::optional<std::reference_wrapper<const google::protobuf::Message>> finalize_config =
        std::nullopt;
  };

  // Executes the replica lifecycle for a stage that discards emitted keys,
  // leaving the host with payloads only.
  template <typename DoFnType>
  static absl::StatusOr<BlobStageOutputs> RunReplicaWithBlobOutput(
      DoFnType& do_fn, const ReplicaInputs& replica_inputs) {
    if constexpr (requires { DoFnType::kConfigSource; }) {
      return RunReplicaWithBlobOutputImpl(do_fn, replica_inputs, DoFnType::kConfigSource,
                                          DoFnType::kRecordSources, DoFnType::kDestinations);
    } else {
      return RunReplicaWithBlobOutputImpl(do_fn, replica_inputs, std::nullopt, DoFnType::kSources,
                                          DoFnType::kDestinations);
    }
  }

  // Executes the replica lifecycle for a stage that keeps emitted keys, leaving
  // the host a plaintext key to route on alongside each payload.
  template <typename DoFnType>
  static absl::StatusOr<KVBlobStageOutputs> RunReplicaWithKVBlobOutput(
      DoFnType& do_fn, const ReplicaInputs& replica_inputs) {
    if constexpr (requires { DoFnType::kConfigSource; }) {
      return RunReplicaWithKVBlobOutputImpl(do_fn, replica_inputs, DoFnType::kConfigSource,
                                            DoFnType::kRecordSources, DoFnType::kDestinations);
    } else {
      return RunReplicaWithKVBlobOutputImpl(do_fn, replica_inputs, std::nullopt, DoFnType::kSources,
                                            DoFnType::kDestinations);
    }
  }

 private:
  static absl::StatusOr<BlobStageOutputs> RunReplicaWithBlobOutputImpl(
      confidential_federated_compute::fns::DoFn& do_fn, const ReplicaInputs& replica_inputs,
      std::optional<Node> config_source, absl::Span<const Node> record_sources,
      absl::Span<const Node> destinations);

  static absl::StatusOr<KVBlobStageOutputs> RunReplicaWithKVBlobOutputImpl(
      confidential_federated_compute::fns::DoFn& do_fn, const ReplicaInputs& replica_inputs,
      std::optional<Node> config_source, absl::Span<const Node> record_sources,
      absl::Span<const Node> destinations);
};

}  // namespace cobalt::trusted_shuffler::testing::integration

#endif  // SRC_TESTING_INTEGRATION_DO_FN_RUNNER_H_
