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

#ifndef SRC_TESTING_INTEGRATION_ENCRYPT_OUTPUT_ROUTER_H_
#define SRC_TESTING_INTEGRATION_ENCRYPT_OUTPUT_ROUTER_H_

#include <vector>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

// Holds the separated outputs emitted across all EncryptOutput shards.
struct EncryptOutputOutputs {
  // Unencrypted EncryptedMessageOutput protos emitted to the orchestrator.
  std::vector<UnencryptedBlob> encrypted_message_outputs;

  // Encrypted PartialReleaseReport protos emitted for the downstream key
  // release verification stage.
  std::vector<EncryptedBlob> partial_release_reports;
};

// Routes IngestionVerificationPayload as Record 0 followed by gathered messages
// as Records 1..N into a single EncryptOutput shard.
absl::StatusOr<std::vector<KVCollection>> RouteToEncryptOutput(
    const EncryptedBlob& verification_payload, absl::Span<const EncryptedBlob> gathered_messages);

// Routes IngestionVerificationPayload as Record 0 and splits gathered messages
// evenly across two EncryptOutput shards.
absl::StatusOr<std::vector<KVCollection>> RouteToTwoShardEncryptOutput(
    const EncryptedBlob& verification_payload, absl::Span<const EncryptedBlob> gathered_messages);

// Fault-injection router that sends an empty input stream to EncryptOutput.
absl::StatusOr<std::vector<KVCollection>> RouteToEncryptOutputWithEmptyInput(
    const EncryptedBlob& verification_payload, absl::Span<const EncryptedBlob> gathered_messages);

// Fault-injection router that places gathered messages before the
// IngestionVerificationPayload instead of as Record 0.
absl::StatusOr<std::vector<KVCollection>> RouteToEncryptOutputWithLateVerificationPayload(
    const EncryptedBlob& verification_payload, absl::Span<const EncryptedBlob> gathered_messages);

// Fault-injection router that injects a message with shuffle_count = 1 into
// EncryptOutput.
absl::StatusOr<std::vector<KVCollection>> RoutePartiallyShuffledMessageToEncryptOutput(
    const EncryptedBlob& verification_payload, absl::Span<const EncryptedBlob> gathered_messages);

// Reads ReleaseOutputHeader on each EncryptOutput record to separate
// EncryptedMessageOutput records from PartialReleaseReports, failing with
// FailedPreconditionError on TYPE_INTEGRITY_FAULT.
absl::StatusOr<EncryptOutputOutputs> SplitEncryptOutputOutputs(
    const KVBlobStageOutputs& encrypt_output);

}  // namespace cobalt::trusted_shuffler::testing::integration

#endif  // SRC_TESTING_INTEGRATION_ENCRYPT_OUTPUT_ROUTER_H_
