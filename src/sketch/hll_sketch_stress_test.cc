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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/proto/sketch.pb.h"
#include "src/sketch/hll_sketch.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::HasSubstr;
using ::testing::Ne;

// -----------------------------------------------------------------------------
// Dimension 1: Saturation Handling, Poisson Inversion Boundary, Integer Overflow
// -----------------------------------------------------------------------------

TEST(HllSketchStressTest, AllRegistersAtRhoMaxReturnsMaxInt64AcrossAllPrecisions) {
  for (int32_t p = HllSketch::kMinPrecision; p <= HllSketch::kMaxPrecision; ++p) {
    const size_t num_buckets = size_t{1} << p;
    const uint32_t max_rho = static_cast<uint32_t>(64 - p + 1);

    HyperLogLogSketch proto;
    proto.set_precision(p);
    for (size_t i = 0; i < num_buckets; ++i) {
      proto.add_registers(max_rho);
    }

    auto sketch = HllSketch::FromProto(proto);
    ASSERT_THAT(sketch, IsOk()) << "Failed to create sketch with max rho for p=" << p;
    EXPECT_FALSE(sketch->IsEmpty());

    const int64_t estimate = sketch->EstimateCardinality();
    // Must return max int64 without crashing, producing NaN, or overflowing to negative.
    EXPECT_EQ(estimate, std::numeric_limits<int64_t>::max())
        << "p=" << p << " did not saturate to int64_t::max()";
  }
}

TEST(HllSketchStressTest, FromProtoEnforcesMaxRhoRejectionAcrossAllPrecisions) {
  for (int32_t p = HllSketch::kMinPrecision; p <= HllSketch::kMaxPrecision; ++p) {
    const size_t num_buckets = size_t{1} << p;
    const uint32_t max_rho = static_cast<uint32_t>(64 - p + 1);

    // proto with max_rho should succeed
    {
      HyperLogLogSketch valid_proto;
      valid_proto.set_precision(p);
      for (size_t i = 0; i < num_buckets; ++i) {
        valid_proto.add_registers(max_rho);
      }
      EXPECT_THAT(HllSketch::FromProto(valid_proto), IsOk());
    }

    // proto with max_rho + 1 must fail with InvalidArgument
    {
      HyperLogLogSketch invalid_proto;
      invalid_proto.set_precision(p);
      invalid_proto.add_registers(max_rho + 1);
      for (size_t i = 1; i < num_buckets; ++i) {
        invalid_proto.add_registers(0);
      }
      EXPECT_THAT(HllSketch::FromProto(invalid_proto),
                  StatusIs(absl::StatusCode::kInvalidArgument,
                           HasSubstr(absl::StrCat("exceeds maximum allowed rho ", max_rho))));
    }
  }
}

TEST(HllSketchStressTest, PoissonInversionBoundarySweepNoNegativeNoNaN) {
  // Test across various precisions p in [4, 18] and various high register settings
  // that push raw estimate across:
  // 1. Below Poisson threshold (E < 2^64 / 30)
  // 2. Across Poisson threshold (E >= 2^64 / 30)
  // 3. Approaching Poisson inversion limit (inverted E* >= 2^63 - 1)
  // 4. At and exceeding raw 2^64 boundary (E >= 2^64)
  for (int32_t p : {4, 6, 10, 14}) {
    const size_t num_buckets = size_t{1} << p;
    const uint32_t max_rho = static_cast<uint32_t>(64 - p + 1);

    // Uniformly test register levels from max_rho - 15 up to max_rho
    const uint32_t start_rho = (max_rho > 15) ? (max_rho - 15) : 1;
    for (uint32_t rho = start_rho; rho <= max_rho; ++rho) {
      HyperLogLogSketch proto;
      proto.set_precision(p);
      for (size_t i = 0; i < num_buckets; ++i) {
        proto.add_registers(rho);
      }

      auto sketch = HllSketch::FromProto(proto);
      ASSERT_THAT(sketch, IsOk());

      const int64_t estimate = sketch->EstimateCardinality();
      // Estimate must be strictly positive and never negative
      EXPECT_GT(estimate, 0) << "Negative or zero estimate for p=" << p << " rho=" << rho;
      // Estimate cannot exceed int64_t::max()
      EXPECT_LE(estimate, std::numeric_limits<int64_t>::max());
    }
  }
}

TEST(HllSketchStressTest, NonUniformHighRegisterPoissonInversionBoundary) {
  // Construct a sketch where half the registers are at max_rho and half at max_rho - 1
  constexpr int32_t kPrecision = 6;
  const size_t num_buckets = size_t{1} << kPrecision;
  const uint32_t max_rho = static_cast<uint32_t>(64 - kPrecision + 1);

  for (size_t num_max = 0; num_max <= num_buckets; num_max += 8) {
    HyperLogLogSketch proto;
    proto.set_precision(kPrecision);
    for (size_t i = 0; i < num_max; ++i) {
      proto.add_registers(max_rho);
    }
    for (size_t i = num_max; i < num_buckets; ++i) {
      proto.add_registers(max_rho - 1);
    }

    auto sketch = HllSketch::FromProto(proto);
    ASSERT_THAT(sketch, IsOk());
    const int64_t estimate = sketch->EstimateCardinality();
    EXPECT_GT(estimate, 0);
    EXPECT_LE(estimate, std::numeric_limits<int64_t>::max());
  }
}

// -----------------------------------------------------------------------------
// Dimension 2: Linear Counting Precision using std::log1p
// -----------------------------------------------------------------------------

TEST(HllSketchStressTest, LinearCountingExactEquivalenceWithOracle) {
  // Verify that for all low cardinality states where raw estimate <= 2.5 * m,
  // the computation matches the exact Linear Counting formula:
  // E* = -m * ln(V / m) = -m * ln(1 - (m - V) / m).
  for (int32_t p : {4, 8, 12, 14}) {
    const size_t m = size_t{1} << p;
    const double m_double = static_cast<double>(m);

    // Test for various counts of non-zero registers (device insertions)
    // from 1 up to min(m / 2, 200)
    const size_t max_nonzero = std::min(m / 2, size_t{200});
    for (size_t non_zero = 1; non_zero <= max_nonzero; non_zero += (p > 8 ? 10 : 1)) {
      HyperLogLogSketch proto;
      proto.set_precision(p);
      for (size_t i = 0; i < non_zero; ++i) {
        proto.add_registers(1);  // register value 1
      }
      for (size_t i = non_zero; i < m; ++i) {
        proto.add_registers(0);  // zero register
      }

      auto sketch = HllSketch::FromProto(proto);
      ASSERT_THAT(sketch, IsOk());

      const int64_t actual_estimate = sketch->EstimateCardinality();

      // Oracle Linear Counting calculation using std::log1p
      const double oracle_double =
          -m_double * std::log1p(-static_cast<double>(non_zero) / m_double);
      const int64_t expected_estimate = static_cast<int64_t>(std::round(oracle_double));

      EXPECT_EQ(actual_estimate, expected_estimate)
          << "Mismatch at p=" << p << " non_zero=" << non_zero;
    }
  }
}

TEST(HllSketchStressTest, LinearCountingNumericalStabilityNearZeroOccupancy) {
  // When m is large (e.g. p = 18, m = 262144) and only 1 register is set (V = m - 1):
  // (m - V) / m = 1 / 262144 = 3.814697265625e-6.
  // Using log(V / m) loses precision because V / m = 0.9999961853 loses lower bits in IEEE 754.
  // std::log1p(-1.0 / m) maintains full precision.
  constexpr int32_t kPrecision = 18;

  auto sketch = HllSketch::Create(kPrecision);
  ASSERT_THAT(sketch, IsOk());
  sketch->Insert("test_single_item");

  const int64_t estimate = sketch->EstimateCardinality();
  // Exactly 1 unique item inserted must estimate to 1
  EXPECT_EQ(estimate, 1);
}

TEST(HllSketchStressTest, LinearCountingMonotonicityAcrossLowCardinalities) {
  // As distinct elements are added, the cardinality estimate must be monotonically non-decreasing.
  auto sketch = HllSketch::Create(14);
  ASSERT_THAT(sketch, IsOk());

  int64_t prev_estimate = 0;
  for (int i = 0; i < 500; ++i) {
    sketch->Insert(absl::StrCat("device_mono_", i));
    const int64_t cur_estimate = sketch->EstimateCardinality();
    EXPECT_GE(cur_estimate, prev_estimate)
        << "Monotonicity violation at insertion " << i << ": prev=" << prev_estimate
        << ", cur=" << cur_estimate;
    prev_estimate = cur_estimate;
  }
}

// -----------------------------------------------------------------------------
// Dimension 3: 128-Bit Shift Summation Determinism & Precision Sweep [4, 18]
// -----------------------------------------------------------------------------

TEST(HllSketchStressTest, IntegerShiftSummationStrictPermutationInvariance) {
  // The primary algorithmic motivation for 128-bit integer shift summation
  // (absl::uint128 shifted_sum += (1 << (max_shift - val))) is that integer addition
  // is strictly commutative and associative: \sum a_i = \sum \pi(a)_i.
  // In contrast, floating-point addition sum += 2^(-val) experiences non-associative rounding
  // drift. We empirically prove that shuffling registers produces 100% identical cardinality
  // estimates.
  constexpr int32_t kPrecision = 10;
  const size_t num_buckets = size_t{1} << kPrecision;

  // Create a realistic non-uniform distribution of register values
  std::vector<uint32_t> regs(num_buckets);
  std::mt19937_64 rng(12345);
  std::geometric_distribution<uint32_t> dist(0.5);  // geometric distribution of ranks

  for (size_t i = 0; i < num_buckets; ++i) {
    regs[i] = std::min(dist(rng) + 1, static_cast<uint32_t>(64 - kPrecision + 1));
  }

  // Baseline estimate
  HyperLogLogSketch proto;
  proto.set_precision(kPrecision);
  for (uint32_t val : regs) {
    proto.add_registers(val);
  }
  auto baseline_sketch = HllSketch::FromProto(proto);
  ASSERT_THAT(baseline_sketch, IsOk());
  const int64_t baseline_estimate = baseline_sketch->EstimateCardinality();

  // Test 50 random permutations of the registers
  for (int trial = 0; trial < 50; ++trial) {
    std::shuffle(regs.begin(), regs.end(), rng);

    HyperLogLogSketch perm_proto;
    perm_proto.set_precision(kPrecision);
    for (uint32_t val : regs) {
      perm_proto.add_registers(val);
    }

    auto perm_sketch = HllSketch::FromProto(perm_proto);
    ASSERT_THAT(perm_sketch, IsOk());
    const int64_t perm_estimate = perm_sketch->EstimateCardinality();

    // Must match bit-for-bit with baseline
    EXPECT_EQ(perm_estimate, baseline_estimate)
        << "Integer shift summation determinism failed on trial " << trial;
  }
}

TEST(HllSketchStressTest, ExhaustivePrecisionsSupport) {
  // Sweep every precision p from 4 to 18.
  // Insert 50 elements, verify estimate is reasonable and no crashes/UB occur.
  for (int32_t p = HllSketch::kMinPrecision; p <= HllSketch::kMaxPrecision; ++p) {
    auto sketch = HllSketch::Create(p);
    ASSERT_THAT(sketch, IsOk()) << "Failed to create sketch for p=" << p;
    EXPECT_EQ(sketch->Precision(), p);
    EXPECT_TRUE(sketch->IsEmpty());
    EXPECT_EQ(sketch->EstimateCardinality(), 0);

    constexpr int kCount = 50;
    for (int i = 0; i < kCount; ++i) {
      sketch->Insert(absl::StrCat("item_", p, "_", i));
    }

    EXPECT_FALSE(sketch->IsEmpty());
    const int64_t estimate = sketch->EstimateCardinality();
    EXPECT_GT(estimate, 0);

    // Standard error is 1.04 / sqrt(2^p)
    const double se = 1.04 / std::sqrt(static_cast<double>(size_t{1} << p));
    // Check estimate is within 4 standard errors or min bound
    const double tolerance = std::max(0.40, 4.0 * se);
    EXPECT_NEAR(static_cast<double>(estimate), static_cast<double>(kCount), kCount * tolerance)
        << "Estimate out of bounds for p=" << p;
  }
}

// -----------------------------------------------------------------------------
// Dimension 4: MergeFrom and Merge Equivalence, Idempotency, and Associativity
// -----------------------------------------------------------------------------

TEST(HllSketchStressTest, MergeAndMergeFromEquivalenceAcrossRandomSketches) {
  // For any pair of sketches A and B:
  // C1 = Merge(A, B)
  // C2 = A; C2.MergeFrom(B)
  // C1 and C2 must have identical registers, identical estimate, and identical IsEmpty().
  std::mt19937_64 rng(42);
  for (int32_t p : {4, 8, 12}) {
    for (int trial = 0; trial < 10; ++trial) {
      auto a = HllSketch::Create(p);
      auto b = HllSketch::Create(p);
      ASSERT_THAT(a, IsOk());
      ASSERT_THAT(b, IsOk());

      const int count_a = rng() % 300;
      const int count_b = rng() % 300;
      for (int i = 0; i < count_a; ++i) {
        a->Insert(absl::StrCat("a_", trial, "_", i));
      }
      for (int i = 0; i < count_b; ++i) {
        b->Insert(absl::StrCat("b_", trial, "_", i));
      }

      // Method 1: Static Merge
      auto merged_static = HllSketch::Merge(*a, *b);
      ASSERT_THAT(merged_static, IsOk());

      // Method 2: In-place MergeFrom
      auto a_copy = *a;
      ASSERT_THAT(a_copy.MergeFrom(*b), IsOk());

      // Compare
      EXPECT_EQ(merged_static->IsEmpty(), a_copy.IsEmpty());
      EXPECT_EQ(merged_static->EstimateCardinality(), a_copy.EstimateCardinality());
      EXPECT_THAT(merged_static->Registers(), ElementsAreArray(a_copy.Registers()));
    }
  }
}

TEST(HllSketchStressTest, MergeCommutativity) {
  // Merge(A, B) must equal Merge(B, A) exactly.
  auto a = HllSketch::Create(10);
  auto b = HllSketch::Create(10);
  ASSERT_THAT(a, IsOk());
  ASSERT_THAT(b, IsOk());

  for (int i = 0; i < 250; ++i) {
    a->Insert(absl::StrCat("elem_a_", i));
  }
  for (int i = 100; i < 350; ++i) {  // partial overlap
    b->Insert(absl::StrCat("elem_b_", i));
  }

  auto ab = HllSketch::Merge(*a, *b);
  auto ba = HllSketch::Merge(*b, *a);
  ASSERT_THAT(ab, IsOk());
  ASSERT_THAT(ba, IsOk());

  EXPECT_EQ(ab->EstimateCardinality(), ba->EstimateCardinality());
  EXPECT_THAT(ab->Registers(), ElementsAreArray(ba->Registers()));
}

TEST(HllSketchStressTest, MergeAssociativity) {
  // (A + B) + C == A + (B + C)
  auto a = HllSketch::Create(10);
  auto b = HllSketch::Create(10);
  auto c = HllSketch::Create(10);
  ASSERT_THAT(a, IsOk());
  ASSERT_THAT(b, IsOk());
  ASSERT_THAT(c, IsOk());

  for (int i = 0; i < 100; ++i)
    a->Insert(absl::StrCat("a_", i));
  for (int i = 0; i < 100; ++i)
    b->Insert(absl::StrCat("b_", i));
  for (int i = 0; i < 100; ++i)
    c->Insert(absl::StrCat("c_", i));

  // (A + B) + C
  auto ab = HllSketch::Merge(*a, *b);
  ASSERT_THAT(ab, IsOk());
  auto ab_c = HllSketch::Merge(*ab, *c);
  ASSERT_THAT(ab_c, IsOk());

  // A + (B + C)
  auto bc = HllSketch::Merge(*b, *c);
  ASSERT_THAT(bc, IsOk());
  auto a_bc = HllSketch::Merge(*a, *bc);
  ASSERT_THAT(a_bc, IsOk());

  EXPECT_EQ(ab_c->EstimateCardinality(), a_bc->EstimateCardinality());
  EXPECT_THAT(ab_c->Registers(), ElementsAreArray(a_bc->Registers()));
}

TEST(HllSketchStressTest, MergeIdempotency) {
  // A + A == A
  auto a = HllSketch::Create(12);
  ASSERT_THAT(a, IsOk());

  for (int i = 0; i < 500; ++i) {
    a->Insert(absl::StrCat("idempotent_test_", i));
  }

  const int64_t before = a->EstimateCardinality();
  auto aa = HllSketch::Merge(*a, *a);
  ASSERT_THAT(aa, IsOk());

  EXPECT_EQ(aa->EstimateCardinality(), before);
  EXPECT_THAT(aa->Registers(), ElementsAreArray(a->Registers()));

  // In-place self-merge
  ASSERT_THAT(a->MergeFrom(*a), IsOk());
  EXPECT_EQ(a->EstimateCardinality(), before);
}

TEST(HllSketchStressTest, CrossPrecisionMergeRejectionMatrix) {
  // Verify that any attempt to merge two sketches with different precisions fails,
  // tested across a matrix of precisions.
  const std::vector<int32_t> test_precisions = {4, 6, 10, 14, 18};
  for (int32_t p1 : test_precisions) {
    for (int32_t p2 : test_precisions) {
      if (p1 == p2)
        continue;

      auto s1 = HllSketch::Create(p1);
      auto s2 = HllSketch::Create(p2);
      ASSERT_THAT(s1, IsOk());
      ASSERT_THAT(s2, IsOk());

      EXPECT_THAT(HllSketch::Merge(*s1, *s2),
                  StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("mismatched precisions")));
      EXPECT_THAT(s1->MergeFrom(*s2),
                  StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("mismatched precisions")));
    }
  }
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
