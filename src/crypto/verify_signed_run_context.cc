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

#include "src/crypto/verify_signed_run_context.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/proto/run_context.pb.h"

namespace cobalt::trusted_shuffler {

absl::Status VerifyRunConfig(const RunConfig& run_config) {
  if (run_config.metric_identifiers().empty()) {
    return absl::InvalidArgumentError("RunConfig metric_identifiers must not be empty.");
  }

  absl::flat_hash_set<std::pair<int32_t, int32_t>> seen_metrics;
  for (const RunConfig::MetricIdentifier& metric_identifier : run_config.metric_identifiers()) {
    if (metric_identifier.project_id() <= 0 || metric_identifier.metric_id() <= 0) {
      return absl::InvalidArgumentError(
          "MetricIdentifier project_id and metric_id must be greater than 0.");
    }
    if (!seen_metrics.emplace(metric_identifier.project_id(), metric_identifier.metric_id())
             .second) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Duplicate MetricIdentifier found: project_id=", metric_identifier.project_id(),
          ", metric_id=", metric_identifier.metric_id(), "."));
    }
  }

  if (run_config.min_epoch_index() < 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("RunConfig min_epoch_index must be greater than or equal to 0, got ",
                     run_config.min_epoch_index(), "."));
  }

  if (run_config.max_epoch_index() < run_config.min_epoch_index()) {
    return absl::InvalidArgumentError(
        absl::StrCat("RunConfig max_epoch_index (", run_config.max_epoch_index(),
                     ") must be greater than or equal to min_epoch_index (",
                     run_config.min_epoch_index(), ")."));
  }

  if (run_config.target_batch_size() <= 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("RunConfig target_batch_size must be greater than 0, got ",
                     run_config.target_batch_size(), "."));
  }

  return absl::OkStatus();
}

absl::Status VerifyRunContext(const RunContext& run_context) {
  if (run_context.run_id().empty()) {
    return absl::InvalidArgumentError("RunContext run_id must not be empty.");
  }

  if (run_context.run_id().size() != kRunIdByteLength) {
    return absl::InvalidArgumentError(absl::StrCat("Invalid run_id length: expected ",
                                                   kRunIdByteLength, " bytes, got ",
                                                   run_context.run_id().size(), "."));
  }

  if (run_context.run_hash_key().empty()) {
    return absl::InvalidArgumentError("RunContext run_hash_key must not be empty.");
  }

  if (run_context.run_hash_key().size() != kRunHashKeyByteLength) {
    return absl::InvalidArgumentError(absl::StrCat("Invalid run_hash_key length: expected ",
                                                   kRunHashKeyByteLength, " bytes, got ",
                                                   run_context.run_hash_key().size(), "."));
  }

  if (!run_context.has_run_config()) {
    return absl::InvalidArgumentError("RunContext run_config must be set.");
  }

  return VerifyRunConfig(run_context.run_config());
}

absl::StatusOr<RunContext> VerifySignedRunContext(const SignedRunContext& signed_run_context) {
  if (signed_run_context.serialized_run_context().empty()) {
    return absl::InvalidArgumentError("SignedRunContext serialized_run_context must not be empty.");
  }

  if (signed_run_context.signature().empty()) {
    return absl::InvalidArgumentError("SignedRunContext signature must not be empty.");
  }

  // TODO(b/559232555): Verify the cryptographic signature on
  // `serialized_run_context` using the Oak container signing public key.

  RunContext run_context;
  if (!run_context.ParseFromString(signed_run_context.serialized_run_context())) {
    return absl::InvalidArgumentError(
        "Failed to parse RunContext proto from serialized_run_context.");
  }

  const absl::Status verification_status = VerifyRunContext(run_context);
  if (!verification_status.ok()) {
    return verification_status;
  }

  return run_context;
}

absl::StatusOr<RunContext> VerifySignedRunContext(absl::string_view serialized_signed_run_context) {
  if (serialized_signed_run_context.empty()) {
    return absl::InvalidArgumentError("Serialized SignedRunContext must not be empty.");
  }

  SignedRunContext signed_run_context;
  if (!signed_run_context.ParseFromString(std::string(serialized_signed_run_context))) {
    return absl::InvalidArgumentError(
        "Failed to parse SignedRunContext proto from serialized bytes.");
  }

  return VerifySignedRunContext(signed_run_context);
}

}  // namespace cobalt::trusted_shuffler
