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

#include "src/containers/run_context_initializer/run_context_initializer_fn.h"

#include <cstdint>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "google/protobuf/any.pb.h"
#include "src/crypto/secure_random.h"
#include "src/proto/run_context.pb.h"

namespace cobalt::trusted_shuffler {

inline constexpr int kReencryptionKeyIndex = 0;

absl::Status RunContextInitializerFn::InitializeReplica(
    google::protobuf::Any config, Context& context) {
  RunConfig run_config;
  if (!config.UnpackTo(&run_config)) {
    return absl::InvalidArgumentError(
        "Failed to unpack configuration as RunConfig");
  }

  if (run_config.metric_identifiers().empty()) {
    return absl::InvalidArgumentError(
        "RunConfig metric_identifiers must not be empty");
  }

  for (const auto& metric_identifier : run_config.metric_identifiers()) {
    if (metric_identifier.project_id() <= 0 ||
        metric_identifier.metric_id() <= 0) {
      return absl::InvalidArgumentError(
          "MetricIdentifier project_id and metric_id must be greater than 0");
    }
  }

  if (run_config.max_epoch_index() < run_config.min_epoch_index()) {
    return absl::InvalidArgumentError(
        "RunConfig max_epoch_index must be greater than or equal to "
        "min_epoch_index");
  }

  if (run_config.target_batch_size() <= 0) {
    return absl::InvalidArgumentError(
        "RunConfig target_batch_size must be greater than 0");
  }

  run_config_ = std::move(run_config);
  return absl::OkStatus();
}

absl::Status RunContextInitializerFn::Do(KV input, Context& context) {
  return absl::OkStatus();
}

absl::Status RunContextInitializerFn::FinalizeReplica(
    google::protobuf::Any config, Context& context) {
  if (!run_config_.has_value()) {
    return absl::FailedPreconditionError(
        "No RunConfig was provided in InitializeReplica");
  }

  if (signing_key_handle_ == nullptr) {
    return absl::InternalError("SigningKeyHandle is null");
  }

  absl::StatusOr<std::string> run_id = GenerateRunId();
  if (!run_id.ok()) {
    return run_id.status();
  }

  absl::StatusOr<std::string> run_hash_key = GenerateRunHashKey();
  if (!run_hash_key.ok()) {
    return run_hash_key.status();
  }

  RunContext run_context;
  run_context.set_run_id(*run_id);
  run_context.set_run_hash_key(*run_hash_key);
  *run_context.mutable_run_config() = *run_config_;

  std::string serialized_run_context = run_context.SerializeAsString();

  absl::StatusOr<oak::crypto::v1::Signature> signature =
      signing_key_handle_->Sign(serialized_run_context);
  if (!signature.ok()) {
    return signature.status();
  }

  SignedRunContext signed_run_context;
  signed_run_context.set_serialized_run_context(serialized_run_context);
  signed_run_context.set_signature(signature->signature());

  context.EmitEncrypted(
      kReencryptionKeyIndex,
      KV(std::move(config), signed_run_context.SerializeAsString()));

  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler
