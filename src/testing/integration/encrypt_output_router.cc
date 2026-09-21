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

#include "src/testing/integration/encrypt_output_router.h"

#include <cstddef>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/release.pb.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

absl::StatusOr<std::vector<KVCollection>> RouteToEncryptOutput(
    const EncryptedBlob& verification_payload, absl::Span<const EncryptedBlob> gathered_messages) {
  KVCollection shard;
  shard.reserve(1 + gathered_messages.size());
  shard.push_back(verification_payload.ToKV());
  for (const EncryptedBlob& msg : gathered_messages) {
    shard.push_back(msg.ToKV());
  }
  return std::vector<KVCollection>{std::move(shard)};
}

absl::StatusOr<std::vector<KVCollection>> RouteToTwoShardEncryptOutput(
    const EncryptedBlob& verification_payload, absl::Span<const EncryptedBlob> gathered_messages) {
  std::vector<KVCollection> shards(2);
  shards[0].push_back(verification_payload.ToKV());
  shards[1].push_back(verification_payload.ToKV());

  for (size_t i = 0; i < gathered_messages.size(); ++i) {
    shards[i % 2].push_back(gathered_messages[i].ToKV());
  }
  return shards;
}

absl::StatusOr<std::vector<KVCollection>> RouteToEncryptOutputWithEmptyInput(
    const EncryptedBlob& /*verification_payload*/,
    absl::Span<const EncryptedBlob> /*gathered_messages*/) {
  return std::vector<KVCollection>{{}};
}

absl::StatusOr<std::vector<KVCollection>> RouteToEncryptOutputWithLateVerificationPayload(
    const EncryptedBlob& verification_payload, absl::Span<const EncryptedBlob> gathered_messages) {
  KVCollection shard;
  shard.reserve(1 + gathered_messages.size());
  for (const EncryptedBlob& msg : gathered_messages) {
    shard.push_back(msg.ToKV());
  }
  shard.push_back(verification_payload.ToKV());
  return std::vector<KVCollection>{std::move(shard)};
}

absl::StatusOr<std::vector<KVCollection>> RoutePartiallyShuffledMessageToEncryptOutput(
    const EncryptedBlob& verification_payload, absl::Span<const EncryptedBlob> gathered_messages) {
  KVCollection shard;
  shard.reserve(2 + gathered_messages.size());
  shard.push_back(verification_payload.ToKV());

  ShuffledMessage invalid_msg;
  invalid_msg.set_shuffle_count(1);
  shard.push_back(EncryptedBlob(invalid_msg.SerializeAsString()).ToKV());

  for (const EncryptedBlob& msg : gathered_messages) {
    shard.push_back(msg.ToKV());
  }
  return std::vector<KVCollection>{std::move(shard)};
}

absl::StatusOr<EncryptOutputOutputs> SplitEncryptOutputOutputs(
    const KVBlobStageOutputs& encrypt_output) {
  EncryptOutputOutputs outputs;

  for (const UnencryptedKVBlob& record : encrypt_output.unencrypted) {
    ReleaseOutputHeader header;
    if (!record.key().UnpackTo(&header)) {
      return absl::InternalError(
          "Failed to unpack ReleaseOutputHeader from unencrypted EncryptOutput record.");
    }
    if (header.type() == ReleaseOutputHeader::TYPE_INTEGRITY_FAULT) {
      return absl::FailedPreconditionError("EncryptOutput emitted an unencrypted IntegrityFault.");
    }
    if (header.type() != ReleaseOutputHeader::TYPE_MESSAGE) {
      return absl::InternalError(
          "Unexpected ReleaseOutputHeader::Type on unencrypted EncryptOutput record.");
    }
    outputs.encrypted_message_outputs.push_back(record.DropKey());
  }

  for (const EncryptedKVBlob& record : encrypt_output.encrypted) {
    ReleaseOutputHeader header;
    if (!record.key().UnpackTo(&header)) {
      return absl::InternalError(
          "Failed to unpack ReleaseOutputHeader from encrypted EncryptOutput record.");
    }
    if (header.type() != ReleaseOutputHeader::TYPE_REPORT) {
      return absl::InternalError(
          "Unexpected ReleaseOutputHeader::Type on encrypted EncryptOutput record.");
    }
    outputs.partial_release_reports.push_back(record.DropKey());
  }

  return outputs;
}

}  // namespace cobalt::trusted_shuffler::testing::integration
