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

#include "src/containers/encrypt_output/encrypt_output_fn.h"

#include <optional>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "src/crypto/multiset_fingerprint.h"
#include "src/crypto/release_encrypter.h"
#include "src/crypto/verify_ingestion_verification_payload.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/release.pb.h"
#include "src/proto/run_context.pb.h"

namespace cobalt::trusted_shuffler {

absl::Status EncryptOutputFn::OnConfig(
    const IngestionVerificationPayload& ingestion_verification_payload, Context& context) {
  absl::StatusOr<VerifiedIngestionVerificationResult> verified_payload =
      VerifyIngestionVerificationPayload(ingestion_verification_payload);
  if (!verified_payload.ok()) {
    return SignalIntegrityFault(verified_payload.status().message(), context);
  }

  absl::StatusOr<ReleaseEncrypter> encrypter =
      ReleaseEncrypter::Create(verified_payload->run_context);
  if (!encrypter.ok()) {
    return SignalIntegrityFault("Failed to initialize ReleaseEncrypter from RunContext.", context);
  }
  release_encrypter_.emplace(std::move(*encrypter));

  run_id_ = verified_payload->run_context.run_id();
  approved_partitions_ = std::move(verified_payload->approved_partitions);
  fingerprint_.emplace(STAGE_RELEASE, /*is_partial=*/true, verified_payload->run_context,
                       MultisetFingerprint::kFingerprintSize);
  return absl::OkStatus();
}

absl::Status EncryptOutputFn::OnRecord(KV record, Context& context) {
  if (has_integrity_fault_) {
    return SignalIntegrityFault(context);
  }
  if (is_finalized_) {
    return absl::FailedPreconditionError("Replica is already finalized.");
  }

  ShuffledMessage message;
  if (!message.ParseFromString(record.data)) {
    return SignalIntegrityFault("Failed to parse input data as ShuffledMessage.", context);
  }

  if (message.shuffle_count() != 2) {
    return SignalIntegrityFault("Message shuffle_count must be exactly 2.", context);
  }

  if (message.run_id() != run_id_) {
    return SignalIntegrityFault("Message run_id mismatch.", context);
  }

  if (!message.has_metric_epoch_key()) {
    return SignalIntegrityFault("Message missing metric_epoch_key.", context);
  }

  const MetricEpochKey& metric_epoch_key = message.metric_epoch_key();
  if (!approved_partitions_.contains(metric_epoch_key)) {
    return SignalIntegrityFault("Message partition not in approved partitions.", context);
  }

  fingerprint_->AddMessage(message);

  if (message.is_padding()) {
    return absl::OkStatus();
  }

  auto ciphertext =
      release_encrypter_->EncryptForRelease(message.customer_message(), /*associated_data=*/"");
  if (!ciphertext.ok()) {
    return SignalIntegrityFault("Failed to encrypt customer message for release.", context);
  }

  EncryptedMessageOutput output;
  *output.mutable_metric_epoch_key() = metric_epoch_key;
  output.set_encrypted_message(std::move(*ciphertext));

  ReleaseOutputHeader header;
  header.set_type(ReleaseOutputHeader::TYPE_MESSAGE);
  google::protobuf::Any key;
  key.PackFrom(header);

  context.EmitUnencrypted(KV(std::move(key), output.SerializeAsString()));
  return absl::OkStatus();
}

absl::Status EncryptOutputFn::SignalIntegrityFault(Context& context) {
  return EmitIntegrityFault("IntegrityFault", context);
}

absl::Status EncryptOutputFn::SignalIntegrityFault(absl::string_view reason, Context& context) {
  return EmitIntegrityFault(absl::StrCat("IntegrityFault: ", reason), context);
}

absl::Status EncryptOutputFn::EmitIntegrityFault(absl::string_view message, Context& context) {
  has_integrity_fault_ = true;
  LOG(ERROR) << message;

  ReleaseOutputHeader header;
  header.set_type(ReleaseOutputHeader::TYPE_INTEGRITY_FAULT);
  google::protobuf::Any key;
  key.PackFrom(header);

  IntegrityFault fault;
  context.EmitUnencrypted(KV(std::move(key), fault.SerializeAsString()));

  const absl::Status status = absl::FailedPreconditionError(message);
  context.EmitError(status);
  return status;
}

absl::Status EncryptOutputFn::FinalizeReplica(google::protobuf::Any config, Context& context) {
  if (has_integrity_fault_) {
    return SignalIntegrityFault(context);
  }
  if (is_finalized_) {
    return absl::FailedPreconditionError("Replica is already finalized.");
  }
  if (!HasConfig() || !Config().has_value()) {
    return SignalIntegrityFault("Configuration was not established before FinalizeReplica.",
                                context);
  }

  is_finalized_ = true;

  // Emit the partial release fingerprint encrypted for the downstream key
  // release verification stage. The untrusted orchestrator collects these
  // PartialReleaseReports along with the unencrypted EncryptedMessageOutput
  // records, passes the reports to the final verification stage to verify
  // fingerprint equality and release RunContext.release_key, and then decrypts
  // the customer messages.
  PartialReleaseReport partial_report;
  *partial_report.mutable_partial_fingerprint() = fingerprint_->Build();

  ReleaseOutputHeader header;
  header.set_type(ReleaseOutputHeader::TYPE_REPORT);
  google::protobuf::Any key;
  key.PackFrom(header);

  context.EmitEncrypted(kReencryptionKeyIndex,
                        KV(std::move(key), partial_report.SerializeAsString()));

  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler
