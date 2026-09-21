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

#ifndef SRC_TESTING_INTEGRATION_KEY_RELEASER_ROUTER_H_
#define SRC_TESTING_INTEGRATION_KEY_RELEASER_ROUTER_H_

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "src/proto/common.pb.h"
#include "src/testing/integration/records.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler::testing::integration {

// Builds the input stream for the KeyReleaser session from the
// IngestionVerificationPayload and all PartialReleaseReports produced by
// EncryptOutput shards.
absl::StatusOr<KVCollection> RouteToKeyReleaser(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedBlob> partial_release_reports);

// Fault-injection router that sends an empty input stream to KeyReleaser.
absl::StatusOr<KVCollection> RouteToKeyReleaserWithEmptyInput(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedBlob> partial_release_reports);

// Fault-injection router that drops one PartialReleaseReport so the release
// multiset fingerprint does not match the canonical ingestion fingerprint.
absl::StatusOr<KVCollection> RouteToKeyReleaserWithDroppedReport(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedBlob> partial_release_reports);

// Fault-injection router that places PartialReleaseReports before the
// IngestionVerificationPayload instead of placing the payload as Record 0.
absl::StatusOr<KVCollection> RouteToKeyReleaserWithLateVerificationPayload(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedBlob> partial_release_reports);

// Inspects the unencrypted DecryptionKeyRelease output from KeyReleaser, fails
// with FailedPreconditionError if an IntegrityFault was emitted, and otherwise
// decrypts every EncryptedMessageOutput using the released Tink AEAD key and
// groups the plaintext customer messages by MetricEpochKey.
absl::StatusOr<absl::flat_hash_map<MetricEpochKey, std::vector<std::string>>>
DecryptAndRouteReleasedMessages(const BlobStageOutputs& key_releaser_output,
                                absl::Span<const UnencryptedBlob> encrypted_message_outputs);

}  // namespace cobalt::trusted_shuffler::testing::integration

#endif  // SRC_TESTING_INTEGRATION_KEY_RELEASER_ROUTER_H_
