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

#include "src/sketch/hll_sketch.h"

#include <openssl/bytestring.h>
#include <openssl/sha.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "absl/log/absl_check.h"
#include "absl/numeric/bits.h"
#include "absl/numeric/int128.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/proto/sketch.pb.h"

namespace cobalt::trusted_shuffler {
namespace {

// Asymptotic limit alpha_infinity = 1 / (2 * ln(2)) ~= 0.7213475204444817.
constexpr double kAlphaInfinity = 0.7213475204444817;

// Returns the bias correction constant alpha_m for precision p (m = 2^p registers).
// These constants originate from Proposition 1 in Section 3 of the original HyperLogLog paper:
// Flajolet, Philippe; Fusy, Éric; Gandouet, Olivier; Meunier, Frédéric (2007).
// "HyperLogLog: the analysis of a near-optimal cardinality estimation algorithm".
//
// Mathematically, alpha_m = (m * \int_0^\infty (\log_2((2+u)/(1+u)))^m du)^(-1).
// For small m (16, 32, 64), precomputed values of this numerical integral are used:
//   - m = 16 (p = 4): alpha_16 = 0.673101517
//   - m = 32 (p = 5): alpha_32 = 0.697121585
//   - m = 64 (p = 6): alpha_64 = 0.709209798
// For m >= 128 (p >= 7), the asymptotic formula alpha_m = alpha_infinity / (1 + 1.079 / m)
// where alpha_infinity = 1 / (2 * ln(2)) ~= 0.7213475204444817.
// 64-bit hash large-range Poisson saturation correction is from Heule et al. (2013).
constexpr double ComputeAlpha(int32_t p) {
  switch (p) {
    case 4:
      return 0.673101517;
    case 5:
      return 0.697121585;
    case 6:
      return 0.709209798;
    default:
      return kAlphaInfinity / (1.0 + 1.079 / static_cast<double>(size_t{1} << p));
  }
}

}  // namespace

HllSketch::HllSketch(int32_t precision)
    : precision_(precision), is_empty_(true), registers_(size_t{1} << precision, 0) {}

absl::StatusOr<HllSketch> HllSketch::Create(int32_t precision) {
  if (precision < kMinPrecision || precision > kMaxPrecision) {
    return absl::InvalidArgumentError(absl::StrCat("Precision must be between ", kMinPrecision,
                                                   " and ", kMaxPrecision, ", got: ", precision));
  }
  return HllSketch(precision);
}

absl::Status HllSketch::MergeFrom(const HllSketch& other) {
  if (precision_ != other.precision_) {
    return absl::InvalidArgumentError(
        absl::StrCat("Cannot merge HllSketch instances with mismatched precisions: ", precision_,
                     " vs ", other.precision_));
  }
  is_empty_ = is_empty_ && other.is_empty_;
  const size_t num_buckets = registers_.size();
  for (size_t i = 0; i < num_buckets; ++i) {
    registers_[i] = std::max(registers_[i], other.registers_[i]);
  }
  return absl::OkStatus();
}

absl::StatusOr<HllSketch> HllSketch::Merge(const HllSketch& a, const HllSketch& b) {
  HllSketch merged = a;
  auto status = merged.MergeFrom(b);
  if (!status.ok()) {
    return status;
  }
  return merged;
}

absl::StatusOr<HllSketch> HllSketch::FromProto(const HyperLogLogSketch& proto) {
  if (proto.precision() < kMinPrecision || proto.precision() > kMaxPrecision) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid HyperLogLogSketch precision: ", proto.precision(),
                     ", must be between ", kMinPrecision, " and ", kMaxPrecision));
  }
  const size_t expected_registers = size_t{1} << proto.precision();
  if (proto.registers().empty()) {
    HllSketch sketch(proto.precision());
    sketch.is_empty_ = true;
    return sketch;
  }
  if (static_cast<size_t>(proto.registers_size()) != expected_registers) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid HyperLogLogSketch registers count: ", proto.registers_size(),
                     ", expected: ", expected_registers));
  }

  HllSketch sketch(proto.precision());
  const uint32_t max_rho = static_cast<uint32_t>(64 - proto.precision() + 1);
  bool any_nonzero = false;
  for (size_t i = 0; i < expected_registers; ++i) {
    const uint32_t reg_val = proto.registers(static_cast<int>(i));
    if (reg_val > max_rho) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Register at index ", i, " has value ", reg_val, ", which exceeds maximum allowed rho ",
          max_rho, " for precision ", proto.precision()));
    }
    // Safe downcast: max_rho is at most 61 (since precision >= 4), which is well below
    // std::numeric_limits<uint8_t>::max() (255). No overflow or narrowing can occur.
    sketch.registers_[i] = static_cast<uint8_t>(reg_val);
    if (reg_val > 0) {
      any_nonzero = true;
    }
  }
  sketch.is_empty_ = !any_nonzero;
  return sketch;
}

HyperLogLogSketch HllSketch::ToProto() const {
  HyperLogLogSketch proto;
  proto.set_precision(precision_);
  proto.mutable_registers()->Assign(registers_.begin(), registers_.end());
  return proto;
}

void HllSketch::Insert(absl::string_view item) {
  uint8_t digest[SHA256_DIGEST_LENGTH];
  SHA256(reinterpret_cast<const uint8_t*>(item.data()), item.size(), digest);
  CBS cbs;
  CBS_init(&cbs, digest, sizeof(digest));
  uint64_t hash = 0;
  ABSL_CHECK(CBS_get_u64(&cbs, &hash));

  // Bit manipulation for HyperLogLog:
  // 1. Bucket index: extract the topmost precision_ bits (MSBs) of the 64-bit hash.
  //    Shift right by (64 - precision_) bits to map to [0, 2^precision_ - 1].
  const size_t index = hash >> (64 - precision_);
  // 2. Remainder: shift left by precision_ to discard the bucket index bits and leave
  //    the remaining (64 - precision_) bits in the upper positions.
  const uint64_t remainder = hash << precision_;
  // 3. Rank rho: 1 + number of leading zeroes in the remainder.
  //    The maximum rank is bounded by rho_max = 64 - precision_ + 1 when all
  //    (64 - precision_) remainder bits are zero.
  const uint8_t max_rho = static_cast<uint8_t>(64 - precision_ + 1);
  const uint8_t rho =
      (remainder == 0) ? max_rho : static_cast<uint8_t>(absl::countl_zero(remainder) + 1);
  registers_[index] = std::max(registers_[index], rho);
  is_empty_ = false;
}

int64_t HllSketch::EstimateCardinality() const {
  if (is_empty_) {
    return 0;
  }

  const size_t num_buckets = size_t{1} << precision_;
  size_t num_zeroes = 0;
  absl::uint128 shifted_sum = 0;
  const int max_shift = 64 - precision_ + 1;

  for (size_t i = 0; i < num_buckets; ++i) {
    const uint8_t val = registers_[i];
    if (val == 0) {
      ++num_zeroes;
    }
    // Exact integer shift summation:
    // Accumulate (1 << (max_shift - val)) into 128-bit unsigned integer shifted_sum.
    // By scaling 2^(-val) by 2^max_shift, all additions are exact integer operations,
    // which completely eliminates floating-point non-associativity drift during accumulation.
    shifted_sum += (uint64_t{1} << (max_shift - val));
  }

  if (num_zeroes == num_buckets) {
    return 0;
  }

  // Reconstruct floating-point harmonic sum:
  // sum = shifted_sum * 2^(-max_shift)
  const double sum = std::ldexp(static_cast<double>(shifted_sum), -max_shift);

  const double m = static_cast<double>(num_buckets);
  const double alpha = ComputeAlpha(precision_);
  double estimate = alpha * m * m / sum;

  // Small range correction (Linear Counting):
  // Flajolet et al. (2007, Section 4.1) proved that when raw estimate E <= 2.5 * m,
  // the sketch operates in the sparse regime where many registers are empty (zero).
  // In this regime, Linear Counting (Whang et al., 1990) based on zero-register
  // occupancy V provides the minimum-variance unbiased estimator: E* = -m * ln(V / m).
  // Formulating with std::log1p(-static_cast<double>(num_buckets - num_zeroes) / m) avoids
  // catastrophic cancellation when num_zeroes is close to m.
  if (estimate <= 2.5 * m) {
    if (num_zeroes > 0) {
      estimate = -m * std::log1p(-static_cast<double>(num_buckets - num_zeroes) / m);
    }
  } else {
    // Large range correction for 64-bit hashes:
    // As established by Flajolet et al. (2007, Section 4.2) and Heule et al. (2013, Section 4),
    // when cardinality approaches hash capacity 2^64, hash collisions begin to saturate
    // registers. When raw estimate E > (2^64 / 30.0), collision probability produces a
    // systematic undercount exceeding the sketch's standard error (1.04 / sqrt(m)).
    // Inverting the Poisson collision expectation E = 2^64 * (1 - exp(-E* / 2^64)) yields:
    // E* = -2^64 * ln(1 - E / 2^64).
    const double two_to_64 = std::ldexp(1.0, 64);
    if (estimate > (two_to_64 / 30.0)) {
      if (estimate >= two_to_64) {
        return std::numeric_limits<int64_t>::max();
      }
      estimate = -two_to_64 * std::log1p(-estimate / two_to_64);
    }
  }

  constexpr double kMaxInt64AsDouble = static_cast<double>(std::numeric_limits<int64_t>::max());
  if (std::isnan(estimate) || estimate >= kMaxInt64AsDouble) {
    return std::numeric_limits<int64_t>::max();
  }
  return static_cast<int64_t>(std::round(std::max(0.0, estimate)));
}

void HllSketch::Reset() {
  std::fill(registers_.begin(), registers_.end(), 0);
  is_empty_ = true;
}

}  // namespace cobalt::trusted_shuffler
