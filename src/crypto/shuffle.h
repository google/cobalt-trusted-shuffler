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

#ifndef SRC_CRYPTO_SHUFFLE_H_
#define SRC_CRYPTO_SHUFFLE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <utility>

#include "absl/functional/function_ref.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace cobalt::trusted_shuffler {

namespace internal {

// Generates an unbiased random integer in [0, upper_bound) given a random 64-bit word
// generator. Uses Lemire's nearly divisionless method (with rejection sampling fallback)
// to eliminate modulo bias.
absl::StatusOr<uint64_t> SampleUniformIndex(uint64_t upper_bound,
                                            absl::FunctionRef<absl::StatusOr<uint64_t>()> next_u64);

// A buffered CSPRNG stream backed by BoringSSL's RAND_bytes.
// Buffers random words to minimize entropy source calls during bulk operations like shuffling.
// Internal buffer memory is cleansed upon destruction.
class SecureRandomStream {
 public:
  SecureRandomStream();
  ~SecureRandomStream();

  // Non-copyable, non-movable.
  SecureRandomStream(const SecureRandomStream&) = delete;
  SecureRandomStream& operator=(const SecureRandomStream&) = delete;

  // Generates a uniform 64-bit integer in [0, upper_bound).
  absl::StatusOr<uint64_t> NextIndex(uint64_t upper_bound);

 private:
  absl::Status RefillBuffer();
  absl::StatusOr<uint64_t> NextRandomWord();

  std::array<uint64_t, 256> buffer_;
  size_t buffer_index_;
};

}  // namespace internal

// Shuffles the elements in the random-access range [first, last) using the
// Fisher-Yates (Knuth) algorithm powered by a CSPRNG (BoringSSL RAND_bytes).
//
// Algorithm Selection & Cryptographic Justification:
// 1. Unbiased uniform distribution: Every permutation of N elements is generated
//    with exact probability 1 / N!. At each step i (from N - 1 down to 1), an element
//    is chosen uniformly from the remaining i + 1 positions, ensuring uniform distribution
//    across all N! permutation states.
// 2. CSPRNG backing: Backed by BoringSSL's RAND_bytes, providing cryptographic forward
//    and backward secrecy. Output cannot be predicted or reversed even if earlier
//    shuffles are known.
// 3. Modulo bias elimination: Bounded integer generation uses rejection sampling
//    (Lemire's method), guaranteeing that no index in [0, i] has higher probability
//    than another.
// 4. Optimal complexity: O(N) linear time and O(1) auxiliary space (in-place swapping).
//
// Requires: RandomIt satisfies C++ RandomAccessIterator requirements.
template <typename RandomIt>
absl::Status SecureShuffle(RandomIt first, RandomIt last) {
  static_assert(std::is_base_of_v<std::random_access_iterator_tag,
                                  typename std::iterator_traits<RandomIt>::iterator_category>,
                "SecureShuffle requires random-access iterators.");

  using difference_type = typename std::iterator_traits<RandomIt>::difference_type;
  const difference_type num_elements = std::distance(first, last);
  if (num_elements < 0) {
    return absl::InvalidArgumentError("Invalid iterator range: first > last.");
  }
  if (num_elements <= 1) {
    return absl::OkStatus();
  }

  internal::SecureRandomStream stream;
  for (difference_type current_index = num_elements - 1; current_index > 0; --current_index) {
    const absl::StatusOr<uint64_t> random_index =
        stream.NextIndex(static_cast<uint64_t>(current_index + 1));
    if (!random_index.ok()) {
      return random_index.status();
    }
    const uint64_t swap_index = *random_index;
    if (static_cast<difference_type>(swap_index) != current_index) {
      using std::swap;
      swap(*(first + current_index), *(first + swap_index));
    }
  }
  return absl::OkStatus();
}

// Convenience overload for containers supporting std::begin() and std::end().
template <typename Container>
absl::Status SecureShuffle(Container& container) {
  using std::begin;
  using std::end;
  return SecureShuffle(begin(container), end(container));
}

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CRYPTO_SHUFFLE_H_
