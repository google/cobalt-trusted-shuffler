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

#include "src/crypto/release_encrypter.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/proto/run_context.pb.h"
#include "tink/aead.h"
#include "tink/aead/aead_config.h"
#include "tink/aead/aead_key_templates.h"
#include "tink/config/global_registry.h"
#include "tink/insecure_secret_key_access.h"
#include "tink/keyset_handle.h"
#include "tink/parameters.h"
#include "tink/proto_keyset_format.h"
#include "tink/proto_parameters_format.h"
#include "tink/secret_data.h"
#include "tink/util/secret_data.h"

namespace cobalt::trusted_shuffler {
namespace {

absl::Status EnsureTinkAeadRegistered() {
  static const absl::Status* const kRegisterStatus =
      new absl::Status(crypto::tink::AeadConfig::Register());
  return *kRegisterStatus;
}

}  // namespace

absl::StatusOr<std::string> ReleaseEncrypter::CreateReleaseKey() {
  if (const absl::Status status = EnsureTinkAeadRegistered(); !status.ok()) {
    return status;
  }

  const absl::StatusOr<std::unique_ptr<crypto::tink::KeysetHandle>> keyset_handle =
      crypto::tink::KeysetHandle::GenerateNew(crypto::tink::AeadKeyTemplates::Aes256GcmNoPrefix(),
                                              crypto::tink::KeyGenConfigGlobalRegistry());
  if (!keyset_handle.ok()) {
    return keyset_handle.status();
  }

  const absl::StatusOr<crypto::tink::SecretData> serialized_keyset =
      crypto::tink::SerializeKeysetToProtoKeysetFormat(
          **keyset_handle, crypto::tink::InsecureSecretKeyAccess::Get());
  if (!serialized_keyset.ok()) {
    return serialized_keyset.status();
  }

  return std::string(crypto::tink::util::SecretDataAsStringView(*serialized_keyset));
}

absl::StatusOr<ReleaseEncrypter> ReleaseEncrypter::Create(const RunContext& run_context) {
  if (const absl::Status status = EnsureTinkAeadRegistered(); !status.ok()) {
    return status;
  }

  const std::string& release_key = run_context.release_key();
  if (release_key.empty()) {
    return absl::InvalidArgumentError("Release key in RunContext must not be empty");
  }

  const absl::StatusOr<crypto::tink::KeysetHandle> keyset_handle =
      crypto::tink::ParseKeysetFromProtoKeysetFormat(release_key,
                                                     crypto::tink::InsecureSecretKeyAccess::Get());
  if (!keyset_handle.ok()) {
    return absl::InvalidArgumentError("Invalid release keyset");
  }

  absl::StatusOr<std::unique_ptr<crypto::tink::Aead>> aead =
      keyset_handle->GetPrimitive<crypto::tink::Aead>(crypto::tink::ConfigGlobalRegistry());
  if (!aead.ok()) {
    return absl::InvalidArgumentError("Failed to create AEAD primitive from release keyset");
  }

  if (const absl::Status status = keyset_handle->Validate(); !status.ok()) {
    return absl::InvalidArgumentError("Invalid release keyset");
  }
  if (keyset_handle->size() != 1) {
    return absl::InvalidArgumentError("Release keyset must contain exactly 1 key");
  }

  const absl::StatusOr<std::unique_ptr<crypto::tink::Parameters>> expected_params =
      crypto::tink::ParseParametersFromProtoFormat(
          crypto::tink::AeadKeyTemplates::Aes256GcmNoPrefix().SerializeAsString());
  if (!expected_params.ok()) {
    return expected_params.status();
  }

  const crypto::tink::KeysetHandle::Entry primary = keyset_handle->GetPrimary();
  if (primary.GetKey()->GetParameters() != **expected_params) {
    return absl::InvalidArgumentError(
        "Release keyset primary key parameters do not match Aes256GcmNoPrefix template");
  }

  return ReleaseEncrypter(release_key, std::move(*aead));
}

ReleaseEncrypter::ReleaseEncrypter(std::string release_key,
                                   std::unique_ptr<crypto::tink::Aead> aead)
    : release_key_(std::move(release_key)), aead_(std::move(aead)) {}

ReleaseEncrypter::ReleaseEncrypter(ReleaseEncrypter&& other) noexcept
    : release_key_(std::move(other.release_key_)), aead_(std::move(other.aead_)) {}

absl::StatusOr<std::string> ReleaseEncrypter::EncryptForRelease(
    absl::string_view plaintext, absl::string_view associated_data) const {
  if (!aead_) {
    return absl::FailedPreconditionError(
        "ReleaseEncrypter is uninitialized or has been moved from");
  }

  return aead_->Encrypt(plaintext, associated_data);
}

absl::StatusOr<std::string> ReleaseEncrypter::DecryptReleased(
    absl::string_view ciphertext, absl::string_view associated_data) const {
  if (!aead_) {
    return absl::FailedPreconditionError(
        "ReleaseEncrypter is uninitialized or has been moved from");
  }

  return aead_->Decrypt(ciphertext, associated_data);
}

const std::string& ReleaseEncrypter::ReleaseKey() const { return release_key_; }

}  // namespace cobalt::trusted_shuffler
