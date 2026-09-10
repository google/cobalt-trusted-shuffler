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

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "google/protobuf/repeated_ptr_field.h"
#include "gtest/gtest.h"
#include "src/proto/sketch.pb.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::ElementsAreArray;
using ::testing::HasSubstr;

MATCHER_P2(IsWithinRelativeError, expected, max_relative_error,
           absl::StrFormat("is within %.2f%% relative error of %v", max_relative_error * 100.0,
                           expected)) {
  if (expected == 0) {
    if (arg != 0) {
      *result_listener << "expected exactly 0, got " << arg;
      return false;
    }
    return true;
  }
  const double relative_error =
      std::abs(static_cast<double>(arg - expected)) / static_cast<double>(expected);
  *result_listener << "relative error is " << (relative_error * 100.0) << "% (actual: " << arg
                   << ", expected: " << expected
                   << ", max allowed: " << (max_relative_error * 100.0) << "%)";
  return relative_error <= max_relative_error;
}

TEST(HllSketchTest, CreateDefaultPrecision) {
  auto sketch = HllSketch::Create();
  ASSERT_THAT(sketch, IsOk());
  EXPECT_EQ(sketch->Precision(), HllSketch::kDefaultPrecision);
  EXPECT_TRUE(sketch->IsEmpty());
  EXPECT_EQ(sketch->EstimateCardinality(), 0);
  EXPECT_EQ(sketch->Registers().size(), 1ULL << HllSketch::kDefaultPrecision);
}

TEST(HllSketchTest, CreateValidPrecisionsSucceeds) {
  auto sketch_min = HllSketch::Create(HllSketch::kMinPrecision);
  ASSERT_THAT(sketch_min, IsOk());
  EXPECT_EQ(sketch_min->Precision(), HllSketch::kMinPrecision);
  EXPECT_TRUE(sketch_min->IsEmpty());
  EXPECT_EQ(sketch_min->EstimateCardinality(), 0);
  EXPECT_EQ(sketch_min->Registers().size(), 1ULL << HllSketch::kMinPrecision);

  auto sketch_default = HllSketch::Create(HllSketch::kDefaultPrecision);
  ASSERT_THAT(sketch_default, IsOk());
  EXPECT_EQ(sketch_default->Precision(), HllSketch::kDefaultPrecision);
  EXPECT_TRUE(sketch_default->IsEmpty());
  EXPECT_EQ(sketch_default->Registers().size(), 1ULL << HllSketch::kDefaultPrecision);

  auto sketch_max = HllSketch::Create(HllSketch::kMaxPrecision);
  ASSERT_THAT(sketch_max, IsOk());
  EXPECT_EQ(sketch_max->Precision(), HllSketch::kMaxPrecision);
  EXPECT_TRUE(sketch_max->IsEmpty());
  EXPECT_EQ(sketch_max->Registers().size(), 1ULL << HllSketch::kMaxPrecision);
}

TEST(HllSketchTest, CreatePrecisionBelowMinimumFails) {
  EXPECT_THAT(HllSketch::Create(HllSketch::kMinPrecision - 1),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Precision must be between")));
}

TEST(HllSketchTest, CreatePrecisionAboveMaximumFails) {
  EXPECT_THAT(HllSketch::Create(HllSketch::kMaxPrecision + 1),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Precision must be between")));
}

TEST(HllSketchTest, EmptySketchReturnsZero) {
  auto sketch = HllSketch::Create(14);
  ASSERT_THAT(sketch, IsOk());
  EXPECT_TRUE(sketch->IsEmpty());
  EXPECT_EQ(sketch->EstimateCardinality(), 0);
}

TEST(HllSketchTest, SingleItemReturnsOne) {
  auto sketch = HllSketch::Create(14);
  ASSERT_THAT(sketch, IsOk());

  sketch->Insert("device_unique_1");
  EXPECT_FALSE(sketch->IsEmpty());
  EXPECT_EQ(sketch->EstimateCardinality(), 1);
}

TEST(HllSketchTest, DuplicateItemsReturnOne) {
  auto sketch = HllSketch::Create(14);
  ASSERT_THAT(sketch, IsOk());

  for (int i = 0; i < 1000; ++i) {
    sketch->Insert("same_device_id");
  }
  EXPECT_FALSE(sketch->IsEmpty());
  EXPECT_EQ(sketch->EstimateCardinality(), 1);
}

TEST(HllSketchTest, InsertEmptyStringSucceeds) {
  auto sketch = HllSketch::Create(14);
  ASSERT_THAT(sketch, IsOk());

  sketch->Insert("");
  EXPECT_FALSE(sketch->IsEmpty());
  EXPECT_EQ(sketch->EstimateCardinality(), 1);

  // Inserting empty string again should not change cardinality
  sketch->Insert("");
  EXPECT_EQ(sketch->EstimateCardinality(), 1);
}

TEST(HllSketchTest, InsertBinaryPayloadWithNullBytes) {
  auto sketch = HllSketch::Create(14);
  ASSERT_THAT(sketch, IsOk());

  std::string binary_item_1("abc\0def\0ghi", 11);
  std::string binary_item_2("abc\0def\0ghj", 11);

  sketch->Insert(binary_item_1);
  EXPECT_FALSE(sketch->IsEmpty());
  EXPECT_EQ(sketch->EstimateCardinality(), 1);

  sketch->Insert(binary_item_2);
  EXPECT_EQ(sketch->EstimateCardinality(), 2);

  // Re-inserting binary_item_1 is idempotent
  sketch->Insert(binary_item_1);
  EXPECT_EQ(sketch->EstimateCardinality(), 2);
}

TEST(HllSketchTest, SmallCardinalityLinearCounting) {
  for (int n : {5, 10, 50, 100, 500, 1000}) {
    auto sketch = HllSketch::Create(14);
    ASSERT_THAT(sketch, IsOk());

    for (int i = 0; i < n; ++i) {
      sketch->Insert(absl::StrCat("privacy_id_", i));
    }

    EXPECT_THAT(sketch->EstimateCardinality(), IsWithinRelativeError(n, 0.05));
  }
}

TEST(HllSketchTest, LargeCardinalityWithinTheoreticalBounds) {
  constexpr int32_t kPrecision = 14;
  constexpr int64_t kTarget = 20000;
  auto sketch = HllSketch::Create(kPrecision);
  ASSERT_THAT(sketch, IsOk());

  for (int64_t i = 0; i < kTarget; ++i) {
    sketch->Insert(absl::StrCat("large_cardinality_device_", i));
  }

  const double standard_error = 1.04 / std::sqrt(static_cast<double>(1ULL << kPrecision));
  // Estimate should be within 3 standard errors (~2.43%)
  EXPECT_THAT(sketch->EstimateCardinality(), IsWithinRelativeError(kTarget, 3.0 * standard_error));
}

TEST(HllSketchTest, CardinalityEstimationAtSmallPrecisions) {
  for (int32_t p : {4, 5, 6}) {
    auto sketch = HllSketch::Create(p);
    ASSERT_THAT(sketch, IsOk());
    EXPECT_EQ(sketch->Precision(), p);

    constexpr int kCount = 40;
    for (int i = 0; i < kCount; ++i) {
      sketch->Insert(absl::StrCat("device_", i));
    }

    // Standard error for small p is large (e.g. 1.04 / sqrt(16) = 26% for p=4).
    // Verify estimate is within reasonable range and does not fail or crash.
    const double standard_error = 1.04 / std::sqrt(static_cast<double>(1ULL << p));
    EXPECT_THAT(sketch->EstimateCardinality(), IsWithinRelativeError(kCount, 3.0 * standard_error));
  }
}

TEST(HllSketchTest, MinAndMaxPrecisionEstimation) {
  // Boundary precision p = 4 (min)
  {
    auto sketch_min = HllSketch::Create(HllSketch::kMinPrecision);
    ASSERT_THAT(sketch_min, IsOk());
    for (int i = 0; i < 20; ++i) {
      sketch_min->Insert(absl::StrCat("min_precision_item_", i));
    }
    const double se_min = 1.04 / std::sqrt(static_cast<double>(1ULL << HllSketch::kMinPrecision));
    EXPECT_THAT(sketch_min->EstimateCardinality(), IsWithinRelativeError(20, 3.0 * se_min));
  }

  // Boundary precision p = 18 (max)
  {
    auto sketch_max = HllSketch::Create(HllSketch::kMaxPrecision);
    ASSERT_THAT(sketch_max, IsOk());
    for (int i = 0; i < 500; ++i) {
      sketch_max->Insert(absl::StrCat("max_precision_item_", i));
    }
    const double se_max = 1.04 / std::sqrt(static_cast<double>(1ULL << HllSketch::kMaxPrecision));
    EXPECT_THAT(sketch_max->EstimateCardinality(), IsWithinRelativeError(500, 3.0 * se_max));
  }
}

TEST(HllSketchTest, SaturatedRegistersReturnMaxInt64WithoutCrashOrNaN) {
  constexpr int32_t kPrecision = 14;
  const size_t num_buckets = 1ULL << kPrecision;
  const uint32_t max_rho = static_cast<uint32_t>(64 - kPrecision + 1);

  HyperLogLogSketch proto;
  proto.set_precision(kPrecision);
  for (size_t i = 0; i < num_buckets; ++i) {
    proto.add_registers(max_rho);
  }

  auto sketch = HllSketch::FromProto(proto);
  ASSERT_THAT(sketch, IsOk());
  EXPECT_FALSE(sketch->IsEmpty());
  EXPECT_EQ(sketch->EstimateCardinality(), std::numeric_limits<int64_t>::max());
}

TEST(HllSketchTest, LargeRangePoissonSaturationReturnsMaxInt64WithoutOverflow) {
  // Test saturation where raw estimate is in the Poisson inversion range but
  // inverting results in an estimate >= 2^63 - 1 (which would overflow int64_t).
  constexpr int32_t kPrecision = 4;
  const size_t num_buckets = 1ULL << kPrecision;  // 16
  const uint32_t high_rho = 60;                   // max_rho is 61 for p=4

  HyperLogLogSketch proto;
  proto.set_precision(kPrecision);
  for (size_t i = 0; i < num_buckets; ++i) {
    proto.add_registers(high_rho);
  }

  auto sketch = HllSketch::FromProto(proto);
  ASSERT_THAT(sketch, IsOk());
  EXPECT_FALSE(sketch->IsEmpty());
  EXPECT_EQ(sketch->EstimateCardinality(), std::numeric_limits<int64_t>::max());
}

TEST(HllSketchTest, ResetRestoresEmptyState) {
  auto sketch = HllSketch::Create(10);
  ASSERT_THAT(sketch, IsOk());

  sketch->Insert("device_1");
  sketch->Insert("device_2");
  EXPECT_FALSE(sketch->IsEmpty());
  EXPECT_GT(sketch->EstimateCardinality(), 0);

  sketch->Reset();
  EXPECT_TRUE(sketch->IsEmpty());
  EXPECT_EQ(sketch->EstimateCardinality(), 0);
  for (uint8_t reg : sketch->Registers()) {
    EXPECT_EQ(reg, 0);
  }
}

TEST(HllSketchTest, MergeDisjointSketchesAccuratelyEstimatesSum) {
  auto s1_status = HllSketch::Create(14);
  auto s2_status = HllSketch::Create(14);
  ASSERT_THAT(s1_status, IsOk());
  ASSERT_THAT(s2_status, IsOk());

  HllSketch s1 = std::move(*s1_status);
  HllSketch s2 = std::move(*s2_status);

  constexpr int kCountPerSketch = 3000;
  for (int i = 0; i < kCountPerSketch; ++i) {
    s1.Insert(absl::StrCat("sketch1_device_", i));
    s2.Insert(absl::StrCat("sketch2_device_", i));
  }

  auto merged = HllSketch::Merge(s1, s2);
  ASSERT_THAT(merged, IsOk());
  EXPECT_FALSE(merged->IsEmpty());

  constexpr int64_t kTotalExpected = kCountPerSketch * 2;
  EXPECT_THAT(merged->EstimateCardinality(), IsWithinRelativeError(kTotalExpected, 0.03));
}

TEST(HllSketchTest, MergeIdenticalSketchesRetainsCardinality) {
  auto s1_status = HllSketch::Create(14);
  auto s2_status = HllSketch::Create(14);
  ASSERT_THAT(s1_status, IsOk());
  ASSERT_THAT(s2_status, IsOk());

  constexpr int kCount = 2500;
  for (int i = 0; i < kCount; ++i) {
    s1_status->Insert(absl::StrCat("common_device_", i));
    s2_status->Insert(absl::StrCat("common_device_", i));
  }

  auto merged = HllSketch::Merge(*s1_status, *s2_status);
  ASSERT_THAT(merged, IsOk());
  EXPECT_THAT(merged->EstimateCardinality(), IsWithinRelativeError(kCount, 0.03));
}

TEST(HllSketchTest, MergeOverlappingSketches) {
  auto s1_status = HllSketch::Create(14);
  auto s2_status = HllSketch::Create(14);
  ASSERT_THAT(s1_status, IsOk());
  ASSERT_THAT(s2_status, IsOk());

  // s1: [0, 3000), s2: [1500, 4500) -> union is [0, 4500)
  for (int i = 0; i < 3000; ++i) {
    s1_status->Insert(absl::StrCat("overlapping_device_", i));
  }
  for (int i = 1500; i < 4500; ++i) {
    s2_status->Insert(absl::StrCat("overlapping_device_", i));
  }

  auto merged = HllSketch::Merge(*s1_status, *s2_status);
  ASSERT_THAT(merged, IsOk());

  constexpr int64_t kExpectedUnion = 4500;
  EXPECT_THAT(merged->EstimateCardinality(), IsWithinRelativeError(kExpectedUnion, 0.03));
}

TEST(HllSketchTest, MergeBothEmptyRemainsEmpty) {
  auto s1_status = HllSketch::Create(10);
  auto s2_status = HllSketch::Create(10);
  ASSERT_THAT(s1_status, IsOk());
  ASSERT_THAT(s2_status, IsOk());

  auto merged = HllSketch::Merge(*s1_status, *s2_status);
  ASSERT_THAT(merged, IsOk());
  EXPECT_TRUE(merged->IsEmpty());
  EXPECT_EQ(merged->EstimateCardinality(), 0);
}

TEST(HllSketchTest, MergeEmptyWithNonEmptyRetainsCardinality) {
  auto empty = HllSketch::Create(14);
  auto non_empty = HllSketch::Create(14);
  ASSERT_THAT(empty, IsOk());
  ASSERT_THAT(non_empty, IsOk());

  for (int i = 0; i < 200; ++i) {
    non_empty->Insert(absl::StrCat("non_empty_device_", i));
  }
  const int64_t original_cardinality = non_empty->EstimateCardinality();
  ASSERT_GT(original_cardinality, 0);

  // Direction 1: empty + non_empty
  auto merged1 = HllSketch::Merge(*empty, *non_empty);
  ASSERT_THAT(merged1, IsOk());
  EXPECT_FALSE(merged1->IsEmpty());
  EXPECT_EQ(merged1->EstimateCardinality(), original_cardinality);
  EXPECT_THAT(merged1->Registers(), ElementsAreArray(non_empty->Registers()));

  // Direction 2: non_empty + empty
  auto merged2 = HllSketch::Merge(*non_empty, *empty);
  ASSERT_THAT(merged2, IsOk());
  EXPECT_FALSE(merged2->IsEmpty());
  EXPECT_EQ(merged2->EstimateCardinality(), original_cardinality);
  EXPECT_THAT(merged2->Registers(), ElementsAreArray(non_empty->Registers()));
}

TEST(HllSketchTest, MergeSelfIsIdempotent) {
  auto sketch = HllSketch::Create(14);
  ASSERT_THAT(sketch, IsOk());

  for (int i = 0; i < 300; ++i) {
    sketch->Insert(absl::StrCat("idempotent_device_", i));
  }
  const int64_t estimate_before = sketch->EstimateCardinality();

  auto merged = HllSketch::Merge(*sketch, *sketch);
  ASSERT_THAT(merged, IsOk());
  EXPECT_EQ(merged->EstimateCardinality(), estimate_before);
  EXPECT_THAT(merged->Registers(), ElementsAreArray(sketch->Registers()));

  // In-place self-merge
  ASSERT_THAT(sketch->MergeFrom(*sketch), IsOk());
  EXPECT_EQ(sketch->EstimateCardinality(), estimate_before);
}

TEST(HllSketchTest, MergeFromInPlaceAccumulates) {
  auto s1 = HllSketch::Create(14);
  auto s2 = HllSketch::Create(14);
  ASSERT_THAT(s1, IsOk());
  ASSERT_THAT(s2, IsOk());

  constexpr int kCount = 1500;
  for (int i = 0; i < kCount; ++i) {
    s1->Insert(absl::StrCat("s1_device_", i));
    s2->Insert(absl::StrCat("s2_device_", i));
  }

  const int64_t s2_estimate_before = s2->EstimateCardinality();

  ASSERT_THAT(s1->MergeFrom(*s2), IsOk());
  EXPECT_FALSE(s1->IsEmpty());

  constexpr int64_t kTotalExpected = kCount * 2;
  EXPECT_THAT(s1->EstimateCardinality(), IsWithinRelativeError(kTotalExpected, 0.03));

  // s2 must remain unaffected
  EXPECT_EQ(s2->EstimateCardinality(), s2_estimate_before);
}

TEST(HllSketchTest, MergeMismatchedPrecisionsFails) {
  auto s1 = HllSketch::Create(10);
  auto s2 = HllSketch::Create(12);
  ASSERT_THAT(s1, IsOk());
  ASSERT_THAT(s2, IsOk());

  EXPECT_THAT(
      HllSketch::Merge(*s1, *s2),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Cannot merge HllSketch instances with mismatched precisions: 10 vs 12")));
}

TEST(HllSketchTest, MergeFromMismatchedPrecisionsFails) {
  auto s1 = HllSketch::Create(10);
  auto s2 = HllSketch::Create(12);
  ASSERT_THAT(s1, IsOk());
  ASSERT_THAT(s2, IsOk());

  EXPECT_THAT(
      s1->MergeFrom(*s2),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Cannot merge HllSketch instances with mismatched precisions: 10 vs 12")));
}

TEST(HllSketchTest, MergeFromProtoAccumulates) {
  auto sketch = HllSketch::Create(14);
  ASSERT_THAT(sketch, IsOk());
  auto other_sketch = HllSketch::Create(14);
  ASSERT_THAT(other_sketch, IsOk());

  constexpr int kCount = 1000;
  for (int i = 0; i < kCount; ++i) {
    sketch->Insert(absl::StrCat("dev_a_", i));
    other_sketch->Insert(absl::StrCat("dev_b_", i));
  }

  const HyperLogLogSketch other_proto = other_sketch->ToProto();
  ASSERT_THAT(sketch->MergeFrom(other_proto), IsOk());
  EXPECT_THAT(sketch->EstimateCardinality(), IsWithinRelativeError(kCount * 2, 0.03));
}

TEST(HllSketchTest, MergeFromEmptyProtoNoOp) {
  auto sketch = HllSketch::Create(14);
  ASSERT_THAT(sketch, IsOk());
  sketch->Insert("device_1");

  HyperLogLogSketch empty_proto;
  empty_proto.set_precision(14);
  ASSERT_THAT(sketch->MergeFrom(empty_proto), IsOk());
  EXPECT_EQ(sketch->EstimateCardinality(), 1);
}

TEST(HllSketchTest, MergeFromProtoMismatchedPrecisionFails) {
  auto sketch = HllSketch::Create(14);
  ASSERT_THAT(sketch, IsOk());

  HyperLogLogSketch mismatched_proto;
  mismatched_proto.set_precision(12);
  EXPECT_THAT(
      sketch->MergeFrom(mismatched_proto),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Cannot merge HyperLogLogSketch proto with mismatched precision: 12 vs 14")));
}

TEST(HllSketchTest, MergeFromProtoInvalidRegistersCountFails) {
  auto sketch = HllSketch::Create(10);
  ASSERT_THAT(sketch, IsOk());

  HyperLogLogSketch bad_proto;
  bad_proto.set_precision(10);
  bad_proto.add_registers(1);
  EXPECT_THAT(sketch->MergeFrom(bad_proto),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid HyperLogLogSketch registers count: 1, expected: 1024")));
}

TEST(HllSketchTest, MergeFromProtoRegisterValueExceedsMaxRhoFails) {
  auto sketch = HllSketch::Create(10);
  ASSERT_THAT(sketch, IsOk());

  HyperLogLogSketch bad_proto;
  bad_proto.set_precision(10);
  const size_t expected_registers = 1ULL << 10;
  for (size_t i = 0; i < expected_registers; ++i) {
    bad_proto.add_registers(0);
  }
  bad_proto.set_registers(5, 56);  // Max rho for p=10 is 64 - 10 + 1 = 55
  EXPECT_THAT(
      sketch->MergeFrom(bad_proto),
      StatusIs(
          absl::StatusCode::kInvalidArgument,
          HasSubstr("Register at index 5 has value 56, which exceeds maximum allowed rho 55")));
}

TEST(HllSketchTest, MergeSpanOfSketchesAccumulates) {
  auto s1 = HllSketch::Create(14);
  auto s2 = HllSketch::Create(14);
  auto s3 = HllSketch::Create(14);
  ASSERT_THAT(s1, IsOk());
  ASSERT_THAT(s2, IsOk());
  ASSERT_THAT(s3, IsOk());

  for (int i = 0; i < 500; ++i) {
    s1->Insert(absl::StrCat("s1_", i));
    s2->Insert(absl::StrCat("s2_", i));
    s3->Insert(absl::StrCat("s3_", i));
  }

  const HllSketch sketches[] = {*s1, *s2, *s3};
  auto merged = HllSketch::Merge(absl::MakeConstSpan(sketches));
  ASSERT_THAT(merged, IsOk());
  EXPECT_THAT(merged->EstimateCardinality(), IsWithinRelativeError(1500, 0.03));
}

TEST(HllSketchTest, MergeEmptySpanOfSketchesFails) {
  EXPECT_THAT(HllSketch::Merge(absl::Span<const HllSketch>()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Cannot merge empty collection of sketches.")));
}

TEST(HllSketchTest, MergeSpanOfProtosAccumulates) {
  auto s1 = HllSketch::Create(14);
  auto s2 = HllSketch::Create(14);
  auto s3 = HllSketch::Create(14);
  ASSERT_THAT(s1, IsOk());
  ASSERT_THAT(s2, IsOk());
  ASSERT_THAT(s3, IsOk());

  for (int i = 0; i < 500; ++i) {
    s1->Insert(absl::StrCat("proto1_", i));
    s2->Insert(absl::StrCat("proto2_", i));
    s3->Insert(absl::StrCat("proto3_", i));
  }

  const HyperLogLogSketch protos[] = {s1->ToProto(), s2->ToProto(), s3->ToProto()};
  auto merged = HllSketch::Merge(absl::MakeConstSpan(protos));
  ASSERT_THAT(merged, IsOk());
  EXPECT_THAT(merged->EstimateCardinality(), IsWithinRelativeError(1500, 0.03));
}

TEST(HllSketchTest, MergeEmptySpanOfProtosFails) {
  EXPECT_THAT(HllSketch::Merge(absl::Span<const HyperLogLogSketch>()),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Cannot merge empty collection of sketch protos.")));
}

TEST(HllSketchTest, MergeRepeatedPtrFieldOfProtosAccumulates) {
  auto s1 = HllSketch::Create(14);
  auto s2 = HllSketch::Create(14);
  ASSERT_THAT(s1, IsOk());
  ASSERT_THAT(s2, IsOk());

  for (int i = 0; i < 500; ++i) {
    s1->Insert(absl::StrCat("rep1_", i));
    s2->Insert(absl::StrCat("rep2_", i));
  }

  google::protobuf::RepeatedPtrField<HyperLogLogSketch> repeated_protos;
  *repeated_protos.Add() = s1->ToProto();
  *repeated_protos.Add() = s2->ToProto();

  auto merged = HllSketch::Merge(repeated_protos);
  ASSERT_THAT(merged, IsOk());
  EXPECT_THAT(merged->EstimateCardinality(), IsWithinRelativeError(1000, 0.03));
}

TEST(HllSketchTest, MergeEmptyRepeatedPtrFieldOfProtosFails) {
  google::protobuf::RepeatedPtrField<HyperLogLogSketch> empty_protos;
  EXPECT_THAT(HllSketch::Merge(empty_protos),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Cannot merge empty collection of sketch protos.")));
}

TEST(HllSketchTest, ProtoRoundtripPreservesStateAndEstimate) {
  auto sketch = HllSketch::Create(14);
  ASSERT_THAT(sketch, IsOk());

  for (int i = 0; i < 500; ++i) {
    sketch->Insert(absl::StrCat("roundtrip_device_", i));
  }

  HyperLogLogSketch proto = sketch->ToProto();
  EXPECT_EQ(proto.precision(), 14);
  EXPECT_EQ(proto.registers_size(), 1ULL << 14);

  auto parsed = HllSketch::FromProto(proto);
  ASSERT_THAT(parsed, IsOk());
  EXPECT_EQ(parsed->Precision(), 14);
  EXPECT_FALSE(parsed->IsEmpty());
  EXPECT_EQ(parsed->EstimateCardinality(), sketch->EstimateCardinality());
  EXPECT_THAT(parsed->Registers(), ElementsAreArray(sketch->Registers()));
}

TEST(HllSketchTest, ProtoRoundtripEmptySketchPreservesEmpty) {
  auto sketch = HllSketch::Create(12);
  ASSERT_THAT(sketch, IsOk());

  HyperLogLogSketch proto = sketch->ToProto();
  EXPECT_EQ(proto.precision(), 12);
  EXPECT_EQ(proto.registers_size(), 1ULL << 12);

  auto parsed = HllSketch::FromProto(proto);
  ASSERT_THAT(parsed, IsOk());
  EXPECT_EQ(parsed->Precision(), 12);
  EXPECT_TRUE(parsed->IsEmpty());
  EXPECT_EQ(parsed->EstimateCardinality(), 0);
}

TEST(HllSketchTest, FromProtoOmittedRegistersInitializesEmpty) {
  HyperLogLogSketch empty_proto;
  empty_proto.set_precision(14);

  auto parsed = HllSketch::FromProto(empty_proto);
  ASSERT_THAT(parsed, IsOk());
  EXPECT_EQ(parsed->Precision(), 14);
  EXPECT_TRUE(parsed->IsEmpty());
  EXPECT_EQ(parsed->EstimateCardinality(), 0);
  EXPECT_EQ(parsed->Registers().size(), 1ULL << 14);
}

TEST(HllSketchTest, FromProtoInvalidPrecisionFails) {
  HyperLogLogSketch bad_proto_low;
  bad_proto_low.set_precision(HllSketch::kMinPrecision - 1);
  EXPECT_THAT(HllSketch::FromProto(bad_proto_low),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid HyperLogLogSketch precision")));

  HyperLogLogSketch bad_proto_high;
  bad_proto_high.set_precision(HllSketch::kMaxPrecision + 1);
  EXPECT_THAT(HllSketch::FromProto(bad_proto_high),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid HyperLogLogSketch precision")));
}

TEST(HllSketchTest, FromProtoInvalidRegistersCountFails) {
  HyperLogLogSketch bad_proto;
  bad_proto.set_precision(10);
  bad_proto.add_registers(1);
  bad_proto.add_registers(2);

  EXPECT_THAT(HllSketch::FromProto(bad_proto),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Invalid HyperLogLogSketch registers count: 2, expected: 1024")));
}

TEST(HllSketchTest, FromProtoExcessRegistersFails) {
  HyperLogLogSketch bad_proto;
  bad_proto.set_precision(10);
  const size_t expected_size = 1ULL << 10;
  for (size_t i = 0; i < expected_size + 1; ++i) {
    bad_proto.add_registers(0);
  }

  EXPECT_THAT(
      HllSketch::FromProto(bad_proto),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("Invalid HyperLogLogSketch registers count: 1025, expected: 1024")));
}

TEST(HllSketchTest, FromProtoRegisterValueExceedsMaxRhoFails) {
  HyperLogLogSketch bad_proto;
  bad_proto.set_precision(14);
  const size_t expected_size = 1ULL << 14;
  for (size_t i = 0; i < expected_size; ++i) {
    bad_proto.add_registers(0);
  }
  // Max rho for precision 14 is 64 - 14 + 1 = 51.
  bad_proto.set_registers(0, 52);

  EXPECT_THAT(HllSketch::FromProto(bad_proto),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("Register at index 0 has value 52, which exceeds "
                                 "maximum allowed rho 51 for precision 14")));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
