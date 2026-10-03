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

#include "src/testing/integration/do_fn_runner.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "containers/fns/do_fn.h"
#include "containers/session.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "src/containers/base/pipeline_nodes.h"
#include "src/testing/integration/records.h"
#include "src/testing/integration/simulated_context.h"

namespace cobalt::trusted_shuffler::testing::integration {
namespace {

bool IsAllowedSource(absl::Span<const Node> allowed_sources, Node source) {
  return std::binary_search(allowed_sources.begin(), allowed_sources.end(), source);
}

absl::Status UnacceptedInputError(Node node) {
  if (node == Node::kNone) {
    return absl::PermissionDeniedError("Session does not accept unencrypted input (Node::kNone).");
  }
  return absl::PermissionDeniedError(absl::StrCat("Session does not accept input from node ",
                                                  NodeName(node), " (", static_cast<int>(node),
                                                  ")."));
}

// Drives a DoFn through its full replica lifecycle and returns the sink holding
// everything it emitted. Every record is still key-bearing here; the public
// methods below project that down to what the stage's host-visible output type
// actually exposes.
absl::StatusOr<SimulatedSessionContext> RunReplica(confidential_federated_compute::fns::DoFn& do_fn,
                                                   const DoFnRunner::ReplicaInputs& replica_inputs,
                                                   std::optional<Node> config_source,
                                                   absl::Span<const Node> record_sources,
                                                   absl::Span<const Node> destinations) {
  SimulatedSessionContext context(destinations);

  google::protobuf::Any init_any;
  if (replica_inputs.init_config.has_value()) {
    init_any.PackFrom(replica_inputs.init_config->get());
  }
  if (absl::Status status = do_fn.InitializeReplica(init_any, context); !status.ok()) {
    return status;
  }

  confidential_federated_compute::fns::DoFn::DoContext do_context(context, {});
  for (size_t i = 0; i < replica_inputs.inputs.size(); ++i) {
    const RoutedKV& input = replica_inputs.inputs[i];
    const bool is_record_zero_config = (i == 0 && config_source.has_value());
    const bool is_allowed = is_record_zero_config ? (input.node == *config_source)
                                                  : IsAllowedSource(record_sources, input.node);
    if (!is_allowed) {
      return UnacceptedInputError(input.node);
    }
    if (absl::Status status = do_fn.Do(
            confidential_federated_compute::Session::KV(input.key, input.data), do_context);
        !status.ok()) {
      return status;
    }
  }

  google::protobuf::Any finalize_any;
  if (replica_inputs.finalize_config.has_value()) {
    finalize_any.PackFrom(replica_inputs.finalize_config->get());
  }
  confidential_federated_compute::fns::Fn::FnContext fn_context(context, {});
  if (absl::Status status = do_fn.FinalizeReplica(finalize_any, fn_context); !status.ok()) {
    return status;
  }

  return context;
}

}  // namespace

absl::StatusOr<BlobStageOutputs> DoFnRunner::RunReplicaWithBlobOutputImpl(
    confidential_federated_compute::fns::DoFn& do_fn, const ReplicaInputs& replica_inputs,
    std::optional<Node> config_source, absl::Span<const Node> record_sources,
    absl::Span<const Node> destinations) {
  absl::StatusOr<SimulatedSessionContext> context =
      RunReplica(do_fn, replica_inputs, config_source, record_sources, destinations);
  if (!context.ok()) {
    return context.status();
  }

  BlobStageOutputs outputs;
  outputs.encrypted.reserve(context->Encrypted().size());
  for (const EncryptedKVBlob& record : context->Encrypted()) {
    outputs.encrypted.push_back(record.DropKey());
  }
  outputs.unencrypted.reserve(context->Unencrypted().size());
  for (const UnencryptedKVBlob& record : context->Unencrypted()) {
    outputs.unencrypted.push_back(record.DropKey());
  }
  return outputs;
}

absl::StatusOr<KVBlobStageOutputs> DoFnRunner::RunReplicaWithKVBlobOutputImpl(
    confidential_federated_compute::fns::DoFn& do_fn, const ReplicaInputs& replica_inputs,
    std::optional<Node> config_source, absl::Span<const Node> record_sources,
    absl::Span<const Node> destinations) {
  absl::StatusOr<SimulatedSessionContext> context =
      RunReplica(do_fn, replica_inputs, config_source, record_sources, destinations);
  if (!context.ok()) {
    return context.status();
  }

  return KVBlobStageOutputs{
      .encrypted = context->Encrypted(),
      .unencrypted = context->Unencrypted(),
  };
}

}  // namespace cobalt::trusted_shuffler::testing::integration
