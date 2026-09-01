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

#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "src/proto/sketch.pb.h"

namespace cobalt::trusted_shuffler {

HllSketch::HllSketch(int32_t precision) : precision_(precision), is_empty_(true) {}

absl::StatusOr<HllSketch> HllSketch::Create(int32_t precision) {
  if (precision < kMinPrecision || precision > kMaxPrecision) {
    return absl::InvalidArgumentError(absl::StrCat("Precision must be between ", kMinPrecision,
                                                   " and ", kMaxPrecision, ", got: ", precision));
  }
  return HllSketch(precision);
}

absl::StatusOr<HllSketch> HllSketch::Merge(const HllSketch& a, const HllSketch& b) {
  if (a.precision_ != b.precision_) {
    return absl::InvalidArgumentError(
        absl::StrCat("Cannot merge HllSketch instances with mismatched precisions: ", a.precision_,
                     " vs ", b.precision_));
  }
  HllSketch merged(a.precision_);
  merged.is_empty_ = a.is_empty_ && b.is_empty_;
  // TODO(b/555370722): Implement register-wise max merge.
  return merged;
}

absl::StatusOr<HllSketch> HllSketch::FromProto(const HyperLogLogSketch& proto) {
  if (proto.precision() < kMinPrecision || proto.precision() > kMaxPrecision) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid HyperLogLogSketch precision: ", proto.precision(),
                     ", must be between ", kMinPrecision, " and ", kMaxPrecision));
  }
  HllSketch sketch(proto.precision());
  if (proto.registers_size() > 0) {
    sketch.is_empty_ = false;
  }
  // TODO(b/555370722): Populate register array from proto.registers().
  return sketch;
}

HyperLogLogSketch HllSketch::ToProto() const {
  HyperLogLogSketch proto;
  proto.set_precision(precision_);
  // TODO(b/555370722): Populate registers.
  return proto;
}

void HllSketch::Insert(absl::string_view item) {
  is_empty_ = false;
  // TODO(b/555370722): Implement HyperLogLog bit hashing and register updates.
}

int64_t HllSketch::EstimateCardinality() const {
  // TODO(b/555370722): Implement HyperLogLog cardinality estimation.
  return 0;
}

void HllSketch::Reset() {
  is_empty_ = true;
  // TODO(b/555370722): Clear registers once implemented.
}

}  // namespace cobalt::trusted_shuffler
