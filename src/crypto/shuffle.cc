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

#include "src/crypto/shuffle.h"

#include <openssl/mem.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cstdint>
#include <functional>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace cobalt::trusted_shuffler {

namespace internal {

absl::StatusOr<uint64_t> SampleUniformIndex(
    const uint64_t upper_bound, absl::FunctionRef<absl::StatusOr<uint64_t>()> next_u64) {
  if (upper_bound == 0) {
    return absl::InvalidArgumentError("upper_bound must be greater than 0.");
  }
  if (upper_bound == 1) {
    return 0;
  }

#if defined(__SIZEOF_INT128__)
  // Lemire's nearly divisionless method for unbiased integer generation in [0, s).
  // Daniel Lemire, "Fast Random Integer Generation in an Interval", ACM TOMACS 2019.
  absl::StatusOr<uint64_t> sample = next_u64();
  if (!sample.ok()) {
    return sample.status();
  }
  uint64_t random_word = *sample;

  __uint128_t full_product =
      static_cast<__uint128_t>(random_word) * static_cast<__uint128_t>(upper_bound);
  uint64_t leftover = static_cast<uint64_t>(full_product);

  if (leftover < upper_bound) {
    // 2^64 mod upper_bound = (-upper_bound) % upper_bound in uint64_t arithmetic.
    const uint64_t rejection_threshold = (-upper_bound) % upper_bound;
    while (leftover < rejection_threshold) {
      sample = next_u64();
      if (!sample.ok()) {
        return sample.status();
      }
      random_word = *sample;
      full_product = static_cast<__uint128_t>(random_word) * static_cast<__uint128_t>(upper_bound);
      leftover = static_cast<uint64_t>(full_product);
    }
  }
  return static_cast<uint64_t>(full_product >> 64);
#else
  // Standard rejection sampling fallback.
  // The threshold for rejecting the upper remainder region to eliminate modulo bias.
  const uint64_t rejection_threshold = (-upper_bound) % upper_bound;
  while (true) {
    absl::StatusOr<uint64_t> sample = next_u64();
    if (!sample.ok()) {
      return sample.status();
    }
    const uint64_t random_word = *sample;
    if (random_word >= rejection_threshold) {
      return random_word % upper_bound;
    }
  }
#endif
}

SecureRandomStream::SecureRandomStream() : buffer_index_(buffer_.size()) { buffer_.fill(0); }

SecureRandomStream::~SecureRandomStream() { OPENSSL_cleanse(buffer_.data(), sizeof(buffer_)); }

absl::Status SecureRandomStream::RefillBuffer() {
  if (RAND_bytes(reinterpret_cast<uint8_t*>(buffer_.data()), sizeof(buffer_)) != 1) {
    return absl::InternalError(
        "RAND_bytes failed to generate cryptographically secure random bytes.");
  }
  buffer_index_ = 0;
  return absl::OkStatus();
}

absl::StatusOr<uint64_t> SecureRandomStream::NextRandomWord() {
  if (buffer_index_ >= buffer_.size()) {
    const absl::Status status = RefillBuffer();
    if (!status.ok()) {
      return status;
    }
  }
  const uint64_t random_word = buffer_[buffer_index_];
  ++buffer_index_;
  return random_word;
}

absl::StatusOr<uint64_t> SecureRandomStream::NextIndex(const uint64_t upper_bound) {
  return SampleUniformIndex(upper_bound,
                            std::bind_front(&SecureRandomStream::NextRandomWord, this));
}

}  // namespace internal

}  // namespace cobalt::trusted_shuffler
