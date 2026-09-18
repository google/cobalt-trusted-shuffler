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

#include <functional>
#include <optional>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "containers/fns/do_fn.h"
#include "containers/session.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "src/testing/integration/records.h"
#include "src/testing/integration/simulated_context.h"

namespace cobalt::trusted_shuffler::testing::integration {
namespace {

// Drives a DoFn through its full replica lifecycle and returns the sink holding
// everything it emitted. Every record is still key-bearing here; the public
// methods below project that down to what the stage's host-visible output type
// actually exposes.
absl::StatusOr<SimulatedSessionContext> RunReplica(
    confidential_federated_compute::fns::DoFn& do_fn,
    const DoFnRunner::ReplicaInputs& replica_inputs) {
  SimulatedSessionContext context;

  google::protobuf::Any init_any;
  if (replica_inputs.init_config.has_value()) {
    init_any.PackFrom(replica_inputs.init_config->get());
  }
  if (absl::Status status = do_fn.InitializeReplica(init_any, context); !status.ok()) {
    return status;
  }

  confidential_federated_compute::fns::DoFn::DoContext do_context(context, {});
  for (const confidential_federated_compute::Session::KV& input : replica_inputs.inputs) {
    if (absl::Status status = do_fn.Do(input, do_context); !status.ok()) {
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

absl::StatusOr<BlobStageOutputs> DoFnRunner::RunReplicaWithBlobOutput(
    confidential_federated_compute::fns::DoFn& do_fn, const ReplicaInputs& replica_inputs) {
  absl::StatusOr<SimulatedSessionContext> context = RunReplica(do_fn, replica_inputs);
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

absl::StatusOr<KVBlobStageOutputs> DoFnRunner::RunReplicaWithKVBlobOutput(
    confidential_federated_compute::fns::DoFn& do_fn, const ReplicaInputs& replica_inputs) {
  absl::StatusOr<SimulatedSessionContext> context = RunReplica(do_fn, replica_inputs);
  if (!context.ok()) {
    return context.status();
  }

  return KVBlobStageOutputs{
      .encrypted = context->Encrypted(),
      .unencrypted = context->Unencrypted(),
  };
}

}  // namespace cobalt::trusted_shuffler::testing::integration
