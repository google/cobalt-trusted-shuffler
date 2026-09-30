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
#include <memory>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "cc/attestation/verification/utils.h"
#include "proto/attestation/eventlog.pb.h"
#include "proto/attestation/evidence.pb.h"
#include "proto/attestation/verification.pb.h"
#include "proto/common.pb.h"
#include "proto/session/messages.pb.h"
#include "proto/tink.pb.h"
#include "src/crypto/release_encrypter.h"
#include "src/proto/run_context.pb.h"
#include "tink/cleartext_keyset_handle.h"
#include "tink/config/global_registry.h"
#include "tink/keyset_handle.h"
#include "tink/keyset_reader.h"
#include "tink/public_key_verify.h"
#include "tink/signature/signature_config.h"
#include "tink/signature/signature_pem_keyset_reader.h"

namespace cobalt::trusted_shuffler {

inline constexpr size_t kEcdsaP256Ieee1363SignatureLength = 64;
inline constexpr size_t kP256RawPublicKeyLength = 64;
inline constexpr size_t kP256UncompressedPublicKeyLength = 65;
inline constexpr char kSec1UncompressedPointPrefix = '\x04';

// ASN.1 DER SubjectPublicKeyInfo prefix for NIST P-256 (secp256r1 / prime256v1)
// uncompressed EC public keys (OID 1.2.840.10045.2.1, OID 1.2.840.10045.3.1.7).
inline constexpr uint8_t kP256SpkiDerPrefix[] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01, 0x06,
    0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00, 0x04};

namespace {

absl::Status EnsureTinkSignatureRegistered() {
  static const absl::Status* const kRegisterStatus =
      new absl::Status(crypto::tink::SignatureConfig::Register());
  return *kRegisterStatus;
}

absl::Status VerifySignature(absl::string_view public_key_bytes, absl::string_view signature,
                             absl::string_view data) {
  if (signature.size() != kEcdsaP256Ieee1363SignatureLength) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid signature length: expected ", kEcdsaP256Ieee1363SignatureLength,
                     "-byte IEEE P1363 ECDSA P-256 signature, got ", signature.size(), "."));
  }

  absl::string_view raw_xy;
  // SEC1 uncompressed elliptic curve points start with single-byte prefix 0x04
  // followed by 32-byte X and 32-byte Y coordinates (65 bytes total).
  if (public_key_bytes.size() == kP256UncompressedPublicKeyLength &&
      public_key_bytes[0] == kSec1UncompressedPointPrefix) {
    raw_xy = public_key_bytes.substr(1);
  } else if (public_key_bytes.size() == kP256RawPublicKeyLength) {
    raw_xy = public_key_bytes;
  } else {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid P-256 public key byte length: expected ", kP256RawPublicKeyLength, " or ",
        kP256UncompressedPublicKeyLength, " bytes, got ", public_key_bytes.size(), "."));
  }

  std::string spki_der(reinterpret_cast<const char*>(kP256SpkiDerPrefix),
                       sizeof(kP256SpkiDerPrefix));
  spki_der.append(raw_xy.data(), raw_xy.size());

  // Wrap the DER-encoded SubjectPublicKeyInfo (SPKI) in PEM headers as required
  // by Tink's SignaturePemKeysetReaderBuilder for PEM_EC public key verification.
  std::string pem = absl::StrCat("-----BEGIN PUBLIC KEY-----\n", absl::Base64Escape(spki_der),
                                 "\n-----END PUBLIC KEY-----\n");

  absl::Status register_status = EnsureTinkSignatureRegistered();
  if (!register_status.ok()) {
    return register_status;
  }

  crypto::tink::SignaturePemKeysetReaderBuilder builder(
      crypto::tink::SignaturePemKeysetReaderBuilder::PemReaderType::PUBLIC_KEY_VERIFY);
  builder.Add({
      .serialized_key = pem,
      .parameters =
          {
              .key_type = crypto::tink::PemKeyType::PEM_EC,
              .algorithm = crypto::tink::PemAlgorithm::ECDSA_IEEE,
              .key_size_in_bits = 256,
              .hash_type = google::crypto::tink::HashType::SHA256,
          },
  });

  absl::StatusOr<std::unique_ptr<crypto::tink::KeysetReader>> reader = builder.Build();
  if (!reader.ok()) {
    return reader.status();
  }

  absl::StatusOr<std::unique_ptr<crypto::tink::KeysetHandle>> handle =
      crypto::tink::CleartextKeysetHandle::Read(std::move(*reader));
  if (!handle.ok()) {
    return handle.status();
  }

  absl::StatusOr<std::unique_ptr<crypto::tink::PublicKeyVerify>> verifier =
      (*handle)->GetPrimitive<crypto::tink::PublicKeyVerify>(crypto::tink::ConfigGlobalRegistry());
  if (!verifier.ok()) {
    return verifier.status();
  }

  absl::Status verify_status = (*verifier)->Verify(signature, data);
  if (!verify_status.ok()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Signature verification failed: ", verify_status.message()));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> ExtractP256SigningPublicKey(
    const oak::session::v1::EndorsedEvidence& endorsed_evidence) {
  if (!endorsed_evidence.has_evidence()) {
    return absl::InvalidArgumentError("EndorsedEvidence does not contain evidence.");
  }
  const auto& evidence = endorsed_evidence.evidence();

  for (const std::string& encoded_event : evidence.event_log().encoded_events()) {
    oak::attestation::v1::Event event;
    if (!event.ParseFromString(encoded_event)) {
      continue;
    }
    if (event.tag() == "ORCHESTRATOR" ||
        event.event().Is<oak::attestation::v1::ContainerLayerData>()) {
      oak::attestation::v1::ContainerLayerData layer_data;
      if (event.event().UnpackTo(&layer_data) && !layer_data.signing_public_key().empty()) {
        return layer_data.signing_public_key();
      }
    }
  }

  // Fallback to Oak's standard signing key extraction (which extracts from
  // ContainerLayerData in the event log or application_keys certificate).
  auto oak_extracted_key = oak::attestation::verification::ExtractSigningPublicKey(evidence);
  if (oak_extracted_key.ok() && !oak_extracted_key->empty()) {
    return *oak_extracted_key;
  }

  return absl::InvalidArgumentError(
      "Failed to extract P-256 ECDSA signing public key from endorsed evidence.");
}

}  // namespace

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

  if (absl::Status status = VerifyRunConfig(run_context.run_config()); !status.ok()) {
    return status;
  }

  if (run_context.release_key().empty()) {
    return absl::InvalidArgumentError("RunContext release_key must not be empty.");
  }

  return ReleaseEncrypter::Create(run_context).status();
}

absl::StatusOr<RunContext> VerifySignedRunContext(const SignedRunContext& signed_run_context) {
  if (signed_run_context.serialized_run_context().empty()) {
    return absl::InvalidArgumentError("SignedRunContext serialized_run_context must not be empty.");
  }

  if (signed_run_context.signature().empty()) {
    return absl::InvalidArgumentError("SignedRunContext signature must not be empty.");
  }

  if (!signed_run_context.has_endorsed_evidence()) {
    return absl::InvalidArgumentError("SignedRunContext endorsed_evidence must be set.");
  }

  if (!signed_run_context.endorsed_evidence().has_evidence()) {
    return absl::InvalidArgumentError("SignedRunContext endorsed_evidence must contain evidence.");
  }

  absl::StatusOr<std::string> public_key =
      ExtractP256SigningPublicKey(signed_run_context.endorsed_evidence());
  if (!public_key.ok()) {
    return public_key.status();
  }

  absl::Status sig_status = VerifySignature(*public_key, signed_run_context.signature(),
                                            signed_run_context.serialized_run_context());
  if (!sig_status.ok()) {
    return sig_status;
  }

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
  if (!signed_run_context.ParseFromArray(serialized_signed_run_context.data(),
                                         serialized_signed_run_context.size())) {
    return absl::InvalidArgumentError(
        "Failed to parse SignedRunContext proto from serialized bytes.");
  }

  return VerifySignedRunContext(signed_run_context);
}

}  // namespace cobalt::trusted_shuffler
