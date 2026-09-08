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

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::SizeIs;

TEST(SecureShuffleTest, EmptyVectorSucceeds) {
  std::vector<int> values;
  EXPECT_THAT(SecureShuffle(values), IsOk());
  EXPECT_TRUE(values.empty());
}

TEST(SecureShuffleTest, EmptyRangeSucceeds) {
  std::vector<int> values = {1, 2, 3};
  EXPECT_THAT(SecureShuffle(values.begin(), values.begin()), IsOk());
  EXPECT_THAT(values, ElementsAre(1, 2, 3));
}

TEST(SecureShuffleTest, SingleElementSucceeds) {
  std::vector<int> values = {42};
  EXPECT_THAT(SecureShuffle(values), IsOk());
  EXPECT_THAT(values, ElementsAre(42));
}

TEST(SecureShuffleTest, InvalidIteratorRangeReturnsError) {
  std::vector<int> values = {1, 2, 3};
  EXPECT_THAT(SecureShuffle(values.end(), values.begin()),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(SecureShuffleTest, DuplicateElementsPreserved) {
  std::vector<int> values = {1, 1, 2, 2, 2, 3, 4, 4};
  const std::vector<int> original = values;
  EXPECT_THAT(SecureShuffle(values), IsOk());
  EXPECT_TRUE(std::is_permutation(values.begin(), values.end(), original.begin()));
}

TEST(SecureShuffleTest, LargeVectorPreservesElementsAndChangesOrder) {
  constexpr int kSize = 1000;
  std::vector<int> values(kSize);
  std::iota(values.begin(), values.end(), 0);
  const std::vector<int> original = values;

  EXPECT_THAT(SecureShuffle(values), IsOk());

  // Multiset is preserved: sorted values match original 0..kSize-1.
  std::vector<int> sorted = values;
  std::sort(sorted.begin(), sorted.end());
  EXPECT_EQ(sorted, original);

  // The order has changed with probability 1 - 1/(1000!).
  EXPECT_NE(values, original);
}

TEST(SecureShuffleTest, ShufflesMoveOnlyTypes) {
  std::vector<std::unique_ptr<int>> ptrs;
  for (int i = 0; i < 10; ++i) {
    ptrs.push_back(std::make_unique<int>(i));
  }
  EXPECT_THAT(SecureShuffle(ptrs), IsOk());
  EXPECT_THAT(ptrs, SizeIs(10));

  std::vector<int> recovered_values;
  for (const std::unique_ptr<int>& ptr : ptrs) {
    ASSERT_NE(ptr, nullptr);
    recovered_values.push_back(*ptr);
  }
  std::sort(recovered_values.begin(), recovered_values.end());
  std::vector<int> expected(10);
  std::iota(expected.begin(), expected.end(), 0);
  EXPECT_EQ(recovered_values, expected);
}

TEST(SecureShuffleTest, WorksWithRawPointersAndStdArray) {
  int raw_array[] = {10, 20, 30, 40, 50};
  EXPECT_THAT(SecureShuffle(raw_array, raw_array + 5), IsOk());
  std::vector<int> from_raw(raw_array, raw_array + 5);
  std::sort(from_raw.begin(), from_raw.end());
  EXPECT_THAT(from_raw, ElementsAre(10, 20, 30, 40, 50));

  std::array<int, 5> std_array = {1, 2, 3, 4, 5};
  EXPECT_THAT(SecureShuffle(std_array), IsOk());
  std::array<int, 5> sorted_array = std_array;
  std::sort(sorted_array.begin(), sorted_array.end());
  EXPECT_THAT(sorted_array, ElementsAre(1, 2, 3, 4, 5));
}

TEST(SecureShuffleTest, PermutationsAreUniformlyDistributed) {
  // Original set: {0, 1, 2} has 3! = 6 permutations.
  // We run N = 60,000 trials. Expected frequency per permutation: E = 10,000.
  // We perform Pearson's chi-squared goodness-of-fit test.
  // Degrees of freedom = 6 - 1 = 5.
  // For df = 5, chi^2 critical value for alpha = 1e-4 is 25.74, alpha = 1e-5 is 28.49.
  // A threshold of chi^2 < 28.5 ensures the test is not flaky, while easily catching
  // any non-uniform PRNG or modulo bias (e.g. naive modulo shuffle yields chi^2 > 700).
  constexpr int kTrials = 60000;
  constexpr double kExpectedPerPermutation = kTrials / 6.0;

  absl::flat_hash_map<std::string, int> permutation_counts;
  std::vector<int> elements = {0, 1, 2};

  for (int trial = 0; trial < kTrials; ++trial) {
    elements = {0, 1, 2};
    ASSERT_THAT(SecureShuffle(elements), IsOk());
    const std::string key = absl::StrCat(elements[0], ",", elements[1], ",", elements[2]);
    ++permutation_counts[key];
  }

  EXPECT_EQ(permutation_counts.size(), 6);

  double chi_squared = 0.0;
  for (const auto& [permutation, count] : permutation_counts) {
    const double difference = static_cast<double>(count) - kExpectedPerPermutation;
    chi_squared += (difference * difference) / kExpectedPerPermutation;

    // Assert tight tolerance: within +/- 10% of expected (10,000 +/- 1,000).
    // Standard deviation sigma = sqrt(60000 * 1/6 * 5/6) ~= 91.3.
    // 1,000 is > 10 * sigma.
    EXPECT_GE(count, 9000) << "Permutation " << permutation << " count: " << count;
    EXPECT_LE(count, 11000) << "Permutation " << permutation << " count: " << count;
  }

  EXPECT_LT(chi_squared, 28.49) << "Chi-squared statistic (" << chi_squared
                                << ") exceeds critical threshold for df=5.";
}

TEST(SecureShuffleTest, PositionalDistributionIsUniform) {
  // For N = 4 elements, each element should land in each of the 4 positions
  // with probability 1/4.
  // We run N = 40,000 trials. Expected count per (element, position): 10,000.
  constexpr int kTrials = 40000;
  constexpr int kElements = 4;
  constexpr double kExpected = kTrials / static_cast<double>(kElements);

  // counts[element][position]
  std::vector<std::vector<int>> counts(kElements, std::vector<int>(kElements, 0));
  std::vector<int> values(kElements);

  for (int trial = 0; trial < kTrials; ++trial) {
    std::iota(values.begin(), values.end(), 0);
    ASSERT_THAT(SecureShuffle(values), IsOk());
    for (int position = 0; position < kElements; ++position) {
      ++counts[values[position]][position];
    }
  }

  for (int element = 0; element < kElements; ++element) {
    double chi_squared = 0.0;
    for (int position = 0; position < kElements; ++position) {
      const double difference = static_cast<double>(counts[element][position]) - kExpected;
      chi_squared += (difference * difference) / kExpected;
      EXPECT_GE(counts[element][position], 9000);
      EXPECT_LE(counts[element][position], 11000);
    }
    // df = 3. Critical value for alpha = 1e-4 is 21.11.
    EXPECT_LT(chi_squared, 21.11) << "Chi-squared for element " << element << " is " << chi_squared;
  }
}

TEST(SampleUniformIndexTest, PropagatesGeneratorError) {
  EXPECT_THAT(
      internal::SampleUniformIndex(
          10, []() -> absl::StatusOr<uint64_t> { return absl::InternalError("entropy failure"); }),
      StatusIs(absl::StatusCode::kInternal));
}

TEST(SampleUniformIndexTest, RejectionSamplingRetriesOnBiasedSample) {
  // Test rejection sampling behavior:
  // When upper_bound = 3, (-3) % 3 = 1 in 64-bit modular arithmetic.
  // In Lemire's method:
  // full_product = random_word * 3. leftover = (uint64_t)full_product.
  // If leftover < upper_bound (i.e. leftover < 3):
  // rejection_threshold = (-3) % 3 = 1.
  // If leftover < rejection_threshold (i.e. leftover < 1, so leftover == 0),
  // the sample is rejected and resampled.
  // For random_word = 0, full_product = 0, leftover = 0 < 1. It resamples!
  int call_count = 0;
  const auto mock_generator = [&call_count]() -> absl::StatusOr<uint64_t> {
    ++call_count;
    if (call_count == 1) {
      return 0ULL;  // Forces leftover = 0 < 1, which triggers rejection in Lemire's method.
    }
    return 0x5555555555555555ULL;
  };

  const absl::StatusOr<uint64_t> result = internal::SampleUniformIndex(3, mock_generator);
  ASSERT_THAT(result, IsOk());

  EXPECT_EQ(call_count, 2);
  EXPECT_LT(*result, 3);
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
