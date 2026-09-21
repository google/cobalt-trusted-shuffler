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

#include "src/testing/integration/key_releaser_router.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "src/crypto/release_encrypter.h"
#include "src/proto/common.pb.h"
#include "src/proto/release.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

absl::StatusOr<KVCollection> RouteToKeyReleaser(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedBlob> partial_release_reports) {
  KVCollection inputs;
  inputs.reserve(1 + partial_release_reports.size());
  inputs.push_back(verification_payload.ToKV());
  for (const EncryptedBlob& report : partial_release_reports) {
    inputs.push_back(report.ToKV());
  }
  return inputs;
}

absl::StatusOr<KVCollection> RouteToKeyReleaserWithEmptyInput(
    const EncryptedBlob& /*verification_payload*/,
    absl::Span<const EncryptedBlob> /*partial_release_reports*/) {
  return KVCollection{};
}

absl::StatusOr<KVCollection> RouteToKeyReleaserWithDroppedReport(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedBlob> partial_release_reports) {
  KVCollection inputs;
  inputs.push_back(verification_payload.ToKV());
  if (!partial_release_reports.empty()) {
    for (size_t i = 0; i + 1 < partial_release_reports.size(); ++i) {
      inputs.push_back(partial_release_reports[i].ToKV());
    }
  }
  return inputs;
}

absl::StatusOr<KVCollection> RouteToKeyReleaserWithLateVerificationPayload(
    const EncryptedBlob& verification_payload,
    absl::Span<const EncryptedBlob> partial_release_reports) {
  KVCollection inputs;
  inputs.reserve(1 + partial_release_reports.size());
  for (const EncryptedBlob& report : partial_release_reports) {
    inputs.push_back(report.ToKV());
  }
  inputs.push_back(verification_payload.ToKV());
  return inputs;
}

absl::StatusOr<absl::flat_hash_map<MetricEpochKey, std::vector<std::string>>>
DecryptAndRouteReleasedMessages(const BlobStageOutputs& key_releaser_output,
                                absl::Span<const UnencryptedBlob> encrypted_message_outputs) {
  if (key_releaser_output.unencrypted.size() != 1) {
    return absl::InternalError(
        absl::StrCat("Expected KeyReleaserFn to emit exactly 1 unencrypted "
                     "DecryptionKeyRelease, but got ",
                     key_releaser_output.unencrypted.size(), "."));
  }

  DecryptionKeyRelease key_release;
  if (!key_release.ParseFromString(key_releaser_output.unencrypted[0].plaintext())) {
    return absl::InternalError("Failed to parse DecryptionKeyRelease from KeyReleaserFn output.");
  }

  if (key_release.has_integrity_fault()) {
    return absl::FailedPreconditionError(
        "KeyReleaserFn emitted an IntegrityFault in DecryptionKeyRelease.");
  }
  if (!key_release.has_released_key_report()) {
    return absl::InternalError(
        "DecryptionKeyRelease has neither released_key_report nor integrity_fault.");
  }

  RunContext key_context;
  key_context.set_release_key(key_release.released_key_report().release_key());
  absl::StatusOr<ReleaseEncrypter> decrypter = ReleaseEncrypter::Create(key_context);
  if (!decrypter.ok()) {
    return decrypter.status();
  }

  absl::flat_hash_map<MetricEpochKey, std::vector<std::string>> routed_messages;
  for (const UnencryptedBlob& blob : encrypted_message_outputs) {
    EncryptedMessageOutput msg_output;
    if (!msg_output.ParseFromString(blob.plaintext())) {
      return absl::InternalError("Failed to parse EncryptedMessageOutput.");
    }
    absl::StatusOr<std::string> plaintext =
        decrypter->DecryptReleased(msg_output.encrypted_message(), /*associated_data=*/"");
    if (!plaintext.ok()) {
      return plaintext.status();
    }
    routed_messages[msg_output.metric_epoch_key()].push_back(*std::move(plaintext));
  }

  return routed_messages;
}

}  // namespace cobalt::trusted_shuffler::testing::integration
