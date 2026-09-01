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

#ifndef SRC_SKETCH_HLL_SKETCH_H_
#define SRC_SKETCH_HLL_SKETCH_H_

#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "src/proto/sketch.pb.h"

namespace cobalt::trusted_shuffler {

// HllSketch is a HyperLogLog cardinality estimator sketch used by pipeline
// ingestion stages to estimate the number of unique devices per partition.
//
// TODO(b/555370722): Implement the actual HyperLogLog algorithm (bit hashing,
// register array updates, and bias-corrected cardinality estimation).
class HllSketch {
 public:
  static constexpr int32_t kDefaultPrecision = 14;
  static constexpr int32_t kMinPrecision = 4;
  static constexpr int32_t kMaxPrecision = 18;

  // Creates an HllSketch with the specified precision.
  // Returns an error if precision is outside [kMinPrecision, kMaxPrecision].
  static absl::StatusOr<HllSketch> Create(int32_t precision);

  // Merges two HllSketch instances into a new sketch.
  // Returns an error if the two sketches have mismatched precisions.
  static absl::StatusOr<HllSketch> Merge(const HllSketch& a, const HllSketch& b);

  // Deserializes an HllSketch from a HyperLogLogSketch protobuf message.
  // Returns an error if the protobuf precision is invalid.
  static absl::StatusOr<HllSketch> FromProto(const HyperLogLogSketch& proto);

  // Converts the sketch to a HyperLogLogSketch protobuf message.
  HyperLogLogSketch ToProto() const;

  // Inserts an element into the sketch.
  void Insert(absl::string_view item);

  // Estimates the number of unique elements inserted into the sketch.
  // In this skeleton implementation, returns 0.
  int64_t EstimateCardinality() const;

  // Returns the precision (number of bucket address bits) of this sketch.
  int32_t Precision() const { return precision_; }

  // Returns true if no elements have been inserted into this sketch.
  bool IsEmpty() const { return is_empty_; }

  // Resets this sketch to the empty state.
  void Reset();

 private:
  explicit HllSketch(int32_t precision);

  int32_t precision_;
  bool is_empty_ = true;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_SKETCH_HLL_SKETCH_H_
