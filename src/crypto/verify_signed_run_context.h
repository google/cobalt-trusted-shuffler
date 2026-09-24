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

#ifndef SRC_CRYPTO_VERIFY_SIGNED_RUN_CONTEXT_H_
#define SRC_CRYPTO_VERIFY_SIGNED_RUN_CONTEXT_H_

#include <cstddef>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/proto/run_context.pb.h"

namespace cobalt::trusted_shuffler {

inline constexpr size_t kRunIdByteLength = 32;
inline constexpr size_t kRunHashKeyByteLength = 32;

// Verifies all fields in a RunConfig:
// - `metric_identifiers` must not be empty.
// - Each `MetricIdentifier` must have `project_id > 0` and `metric_id > 0`.
// - `metric_identifiers` must not contain duplicate (project_id, metric_id) pairs.
// - `min_epoch_index` must be non-negative (>= 0).
// - `max_epoch_index` must be greater than or equal to `min_epoch_index`.
// - `target_batch_size` must be greater than 0.
absl::Status VerifyRunConfig(const RunConfig& run_config);

// Verifies all fields in a RunContext:
// - `run_id` must not be empty and must be exactly 32 bytes.
// - `run_hash_key` must not be empty and must be exactly 32 bytes.
// - `run_config` must be set, and its fields are verified recursively via
//   VerifyRunConfig.
// - `release_key` must not be empty and must be a valid serialized Tink
//   AES-256-GCM AEAD release keyset.
//
// Requires Tink AEAD primitives to be registered via
// `crypto::tink::AeadConfig::Register()` prior to calling this function.
absl::Status VerifyRunContext(const RunContext& run_context);

// Verifies a SignedRunContext:
// - `serialized_run_context` must not be empty.
// - `signature` must not be empty and must be a valid 64-byte IEEE P1363 ECDSA
//   P-256 signature.
// - `endorsed_evidence` must not be empty and must contain valid Oak attestation
//   evidence bearing the P-256 ECDSA signing public key (measured into the DICE
//   event log under ContainerLayerData).
// - Verifies the signature over `serialized_run_context` using Tink PublicKeyVerify
//   against the extracted signing public key.
// - Parses the serialized bytes into a RunContext proto.
// - Verifies all fields in the RunContext (recursively).
// Returns the verified RunContext, or an error status on failure.
absl::StatusOr<RunContext> VerifySignedRunContext(const SignedRunContext& signed_run_context);

// Overload taking serialized bytes of a SignedRunContext protocol buffer message.
absl::StatusOr<RunContext> VerifySignedRunContext(absl::string_view serialized_signed_run_context);

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CRYPTO_VERIFY_SIGNED_RUN_CONTEXT_H_
