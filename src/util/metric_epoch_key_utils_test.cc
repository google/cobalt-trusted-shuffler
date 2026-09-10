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

#include "src/util/metric_epoch_key_utils.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/proto/common.pb.h"

namespace cobalt::trusted_shuffler {
namespace {

MetricEpochKey MakeKey(int32_t project_id, int32_t metric_id, int64_t epoch_index) {
  MetricEpochKey key;
  key.set_project_id(project_id);
  key.set_metric_id(metric_id);
  key.set_epoch_index(epoch_index);
  return key;
}

TEST(MetricEpochKeyUtilsTest, EqualityOperatorWorks) {
  MetricEpochKey key1 = MakeKey(10, 20, 100);
  MetricEpochKey key2 = MakeKey(10, 20, 100);
  MetricEpochKey key3 = MakeKey(10, 20, 101);
  MetricEpochKey key4 = MakeKey(10, 21, 100);
  MetricEpochKey key5 = MakeKey(11, 20, 100);

  EXPECT_EQ(key1, key2);
  EXPECT_FALSE(key1 == key3);
  EXPECT_FALSE(key1 == key4);
  EXPECT_FALSE(key1 == key5);
}

TEST(MetricEpochKeyUtilsTest, FlatHashSetSupportsMetricEpochKey) {
  absl::flat_hash_set<MetricEpochKey> set;
  MetricEpochKey key1 = MakeKey(10, 20, 100);
  MetricEpochKey key2 = MakeKey(10, 20, 100);
  MetricEpochKey key3 = MakeKey(10, 20, 101);

  set.insert(key1);
  EXPECT_EQ(set.size(), 1);
  EXPECT_TRUE(set.contains(key2));
  EXPECT_FALSE(set.contains(key3));

  set.insert(key3);
  EXPECT_EQ(set.size(), 2);
}

TEST(MetricEpochKeyUtilsTest, FlatHashMapSupportsMetricEpochKey) {
  absl::flat_hash_map<MetricEpochKey, int> map;
  MetricEpochKey key1 = MakeKey(10, 20, 100);
  MetricEpochKey key2 = MakeKey(10, 20, 100);

  map[key1] = 42;
  EXPECT_EQ(map.size(), 1);
  EXPECT_EQ(map[key2], 42);
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
