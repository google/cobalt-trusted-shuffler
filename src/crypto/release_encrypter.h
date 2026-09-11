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

#ifndef SRC_CRYPTO_RELEASE_ENCRYPTER_H_
#define SRC_CRYPTO_RELEASE_ENCRYPTER_H_

#include <memory>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/proto/run_context.pb.h"
#include "tink/aead.h"

namespace cobalt::trusted_shuffler {

// Authenticated symmetric encryption and decryption using Tink AES-256-GCM
// for confidential release pipelines.
//
// Tink AEAD primitives are automatically registered on first use via
// `crypto::tink::AeadConfig::Register()`.
class ReleaseEncrypter {
 public:
  // Generates a new Tink AES-256-GCM (no prefix) keyset and returns its
  // serialized protobuf representation (ProtoKeysetFormat), suitable for
  // populating the `release_key` field in `RunContext`.
  static absl::StatusOr<std::string> CreateReleaseKey();

  // Creates a ReleaseEncrypter by parsing the serialized Tink keyset from
  // `run_context.release_key()`.
  static absl::StatusOr<ReleaseEncrypter> Create(const RunContext& run_context);

  ~ReleaseEncrypter() = default;
  ReleaseEncrypter(ReleaseEncrypter&&) noexcept;
  ReleaseEncrypter& operator=(ReleaseEncrypter&&) = delete;
  ReleaseEncrypter(const ReleaseEncrypter&) = delete;
  ReleaseEncrypter& operator=(const ReleaseEncrypter&) = delete;

  // Encrypts plaintext for release using Tink AES-256-GCM with a fresh random
  // 12-byte IV and associated data. Returns ciphertext formatted as:
  // [12-byte IV] + [ciphertext] + [16-byte authentication tag].
  absl::StatusOr<std::string> EncryptForRelease(absl::string_view plaintext,
                                                absl::string_view associated_data) const;

  // Decrypts ciphertext that was encrypted for release using Tink AES-256-GCM
  // and associated data.
  absl::StatusOr<std::string> DecryptReleased(absl::string_view ciphertext,
                                              absl::string_view associated_data) const;

  // Returns the serialized Tink release keyset.
  const std::string& ReleaseKey() const;

 private:
  ReleaseEncrypter(std::string release_key, std::unique_ptr<crypto::tink::Aead> aead);

  std::string release_key_;
  std::unique_ptr<crypto::tink::Aead> aead_;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CRYPTO_RELEASE_ENCRYPTER_H_
