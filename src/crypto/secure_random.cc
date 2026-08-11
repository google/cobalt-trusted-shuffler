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

#include "src/crypto/secure_random.h"

#include <openssl/rand.h>

#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace cobalt::trusted_shuffler {

absl::StatusOr<std::string> GenerateCryptoRandomBytes(std::size_t num_bytes) {
  std::string bytes(num_bytes, '\0');
  if (RAND_bytes(reinterpret_cast<uint8_t*>(bytes.data()), num_bytes) != 1) {
    return absl::InternalError(
        "RAND_bytes failed to generate cryptographically secure random bytes.");
  }
  return bytes;
}

absl::StatusOr<std::string> GenerateRunId() {
  return GenerateCryptoRandomBytes(32);
}

absl::StatusOr<std::string> GenerateRunHashKey() {
  return GenerateCryptoRandomBytes(32);
}

}  // namespace cobalt::trusted_shuffler

