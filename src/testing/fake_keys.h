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

#ifndef SRC_TESTING_FAKE_KEYS_H_
#define SRC_TESTING_FAKE_KEYS_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "cc/crypto/encryption_key.h"
#include "cc/crypto/signing_key.h"

namespace cobalt::trusted_shuffler::testing {

// Fake implementation of SigningKeyHandle that does not implement any
// functionality.
class FakeSigningKey : public oak::crypto::SigningKeyHandle {
 public:
  absl::StatusOr<::oak::crypto::v1::Signature> Sign(absl::string_view message) override {
    return absl::UnimplementedError("FakeSigningKey::Sign not implemented");
  }
};

// Fake implementation of SigningKeyHandle that does not implement any
// functionality.
class FakeEncryptionKeyHandle : public oak::crypto::EncryptionKeyHandle {
 public:
  absl::StatusOr<std::unique_ptr<oak::crypto::RecipientContext>> GenerateRecipientContext(
      absl::string_view serialized_encapsulated_public_key) override {
    return absl::UnimplementedError(
        "FakeEncryptionKeyHandle::GenerateRecipientContext not implemented");
  }
};

}  // namespace cobalt::trusted_shuffler::testing

#endif  // SRC_TESTING_FAKE_KEYS_H_