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

#ifndef SRC_CRYPTO_SECURE_RANDOM_H_
#define SRC_CRYPTO_SECURE_RANDOM_H_

#include <cstddef>
#include <string>

#include "absl/status/statusor.h"

namespace cobalt::trusted_shuffler {

// Generates num_bytes of cryptographically secure random bytes using
// BoringSSL's RAND_bytes.
absl::StatusOr<std::string> GenerateCryptoRandomBytes(std::size_t num_bytes);

// Generates a 32-byte cryptographically secure unique identifier for a run.
absl::StatusOr<std::string> GenerateRunId();

// Generates a 32-byte cryptographically secure secret key used to hash incoming
// data.
absl::StatusOr<std::string> GenerateRunHashKey();

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CRYPTO_SECURE_RANDOM_H_

