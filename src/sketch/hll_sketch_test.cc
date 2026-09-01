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

#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/proto/sketch.pb.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;

TEST(HllSketchTest, CreateValidPrecisionsSucceeds) {
  auto sketch_min = HllSketch::Create(HllSketch::kMinPrecision);
  ASSERT_THAT(sketch_min, IsOk());
  EXPECT_EQ(sketch_min->Precision(), HllSketch::kMinPrecision);
  EXPECT_TRUE(sketch_min->IsEmpty());
  EXPECT_EQ(sketch_min->EstimateCardinality(), 0);

  auto sketch_default = HllSketch::Create(HllSketch::kDefaultPrecision);
  ASSERT_THAT(sketch_default, IsOk());
  EXPECT_EQ(sketch_default->Precision(), HllSketch::kDefaultPrecision);
  EXPECT_TRUE(sketch_default->IsEmpty());

  auto sketch_max = HllSketch::Create(HllSketch::kMaxPrecision);
  ASSERT_THAT(sketch_max, IsOk());
  EXPECT_EQ(sketch_max->Precision(), HllSketch::kMaxPrecision);
  EXPECT_TRUE(sketch_max->IsEmpty());
}

TEST(HllSketchTest, CreatePrecisionBelowMinimumFails) {
  EXPECT_THAT(HllSketch::Create(HllSketch::kMinPrecision - 1),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Precision must be between")));
}

TEST(HllSketchTest, CreatePrecisionAboveMaximumFails) {
  EXPECT_THAT(HllSketch::Create(HllSketch::kMaxPrecision + 1),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("Precision must be between")));
}

TEST(HllSketchTest, InsertMarksSketchNonEmptyAndRetainsNoOpEstimate) {
  auto sketch = HllSketch::Create(12);
  ASSERT_THAT(sketch, IsOk());
  EXPECT_TRUE(sketch->IsEmpty());

  sketch->Insert("privacy_id_1");
  EXPECT_FALSE(sketch->IsEmpty());
  EXPECT_EQ(sketch->EstimateCardinality(), 0);

  sketch->Insert("privacy_id_2");
  EXPECT_FALSE(sketch->IsEmpty());
  EXPECT_EQ(sketch->EstimateCardinality(), 0);
}

TEST(HllSketchTest, ResetRestoresEmptyState) {
  auto sketch = HllSketch::Create(10);
  ASSERT_THAT(sketch, IsOk());

  sketch->Insert("device_1");
  EXPECT_FALSE(sketch->IsEmpty());

  sketch->Reset();
  EXPECT_TRUE(sketch->IsEmpty());
  EXPECT_EQ(sketch->EstimateCardinality(), 0);
}

TEST(HllSketchTest, MergeCombinesSketchesWithoutMutatingInputs) {
  auto s1_status = HllSketch::Create(12);
  auto s2_status = HllSketch::Create(12);
  ASSERT_THAT(s1_status, IsOk());
  ASSERT_THAT(s2_status, IsOk());

  HllSketch s1 = std::move(*s1_status);
  HllSketch s2 = std::move(*s2_status);
  s2.Insert("device_from_s2");

  EXPECT_TRUE(s1.IsEmpty());
  EXPECT_FALSE(s2.IsEmpty());

  auto merged = HllSketch::Merge(s1, s2);
  ASSERT_THAT(merged, IsOk());
  EXPECT_EQ(merged->Precision(), 12);
  EXPECT_FALSE(merged->IsEmpty());

  // Input sketches remain unchanged
  EXPECT_TRUE(s1.IsEmpty());
  EXPECT_FALSE(s2.IsEmpty());
}

TEST(HllSketchTest, MergeBothEmptyRemainsEmpty) {
  auto s1_status = HllSketch::Create(10);
  auto s2_status = HllSketch::Create(10);
  ASSERT_THAT(s1_status, IsOk());
  ASSERT_THAT(s2_status, IsOk());

  auto merged = HllSketch::Merge(*s1_status, *s2_status);
  ASSERT_THAT(merged, IsOk());
  EXPECT_TRUE(merged->IsEmpty());
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

TEST(HllSketchTest, ProtoRoundtripPreservesPrecision) {
  auto sketch = HllSketch::Create(14);
  ASSERT_THAT(sketch, IsOk());
  sketch->Insert("device_123");

  HyperLogLogSketch proto = sketch->ToProto();
  EXPECT_EQ(proto.precision(), 14);

  auto parsed = HllSketch::FromProto(proto);
  ASSERT_THAT(parsed, IsOk());
  EXPECT_EQ(parsed->Precision(), 14);
  EXPECT_EQ(parsed->EstimateCardinality(), 0);
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

}  // namespace
}  // namespace cobalt::trusted_shuffler
