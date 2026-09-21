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

#include "src/testing/integration/scatter_shuffle_router.h"

#include <string>
#include <vector>

#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/proto/common.pb.h"
#include "src/testing/integration/records.h"
#include "src/testing/test_utils.h"

namespace cobalt::trusted_shuffler::testing::integration {
namespace {

using ::absl_testing::StatusIs;
using ::cobalt::trusted_shuffler::testing::CreateMetricEpochKey;
using ::testing::HasSubstr;
using ::testing::SizeIs;

EncryptedKVBlob MakeEncryptedKVBlob(const MetricEpochKey& key, absl::string_view payload) {
  google::protobuf::Any any_key;
  any_key.PackFrom(key);

  ShuffledMessage msg;
  msg.set_shuffle_count(0);
  msg.set_customer_message(std::string(payload));

  return EncryptedKVBlob(any_key, msg.SerializeAsString());
}

TEST(ScatterShuffleRouterTest, GroupsAndBatchesMessagesByPartitionCorrectly) {
  const EncryptedBlob verification_payload("payload");

  MetricEpochKey key1 =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 2, .epoch_index = 0});
  MetricEpochKey key2 =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 3, .epoch_index = 0});

  std::vector<EncryptedKVBlob> real_msgs = {
      MakeEncryptedKVBlob(key1, "real_k1_1"),
      MakeEncryptedKVBlob(key2, "real_k2_1"),
      MakeEncryptedKVBlob(key1, "real_k1_2"),
  };

  std::vector<EncryptedKVBlob> padding_msgs = {
      MakeEncryptedKVBlob(key1, "pad_k1_1"), MakeEncryptedKVBlob(key1, "pad_k1_2"),
      MakeEncryptedKVBlob(key2, "pad_k2_1"), MakeEncryptedKVBlob(key2, "pad_k2_2"),
      MakeEncryptedKVBlob(key2, "pad_k2_3"),
  };

  // For key1: 2 real + 2 padding = 4 msgs
  // For key2: 1 real + 3 padding = 4 msgs
  // Batch size 2 should produce 4 batches, each with verification payload + 2 msgs.
  absl::StatusOr<std::vector<KVCollection>> result =
      RouteToScatterShuffle(verification_payload, /*target_batch_size=*/2, real_msgs, padding_msgs);

  ASSERT_TRUE(result.ok());
  ASSERT_THAT(*result, SizeIs(4));

  for (const KVCollection& batch : *result) {
    ASSERT_THAT(batch, SizeIs(3));
    EXPECT_EQ(batch[0].data, "payload");
    // Keys shouldn't be empty for msg 1 and 2
    EXPECT_FALSE(batch[1].key.type_url().empty());
    EXPECT_FALSE(batch[2].key.type_url().empty());
  }
}

TEST(ScatterShuffleRouterTest, SplitsMultiplesOfTargetBatchSizeIntoSeparateBatches) {
  const EncryptedBlob verification_payload("payload");

  std::vector<EncryptedKVBlob> thresholds;
  std::vector<EncryptedKVBlob> padding;

  int32_t target_batch_size = 5;

  MetricEpochKey key1 =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 1, .epoch_index = 1});
  MetricEpochKey key2 =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 2, .epoch_index = 1});
  MetricEpochKey key3 =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 3, .epoch_index = 1});

  // Partition 1: exactly target_batch_size (3 real, 2 padding)
  for (int i = 0; i < 3; i++)
    thresholds.push_back(MakeEncryptedKVBlob(key1, "r"));
  for (int i = 0; i < 2; i++)
    padding.push_back(MakeEncryptedKVBlob(key1, "p"));

  // Partition 2: 2 * target_batch_size (10 real, 0 padding)
  for (int i = 0; i < 10; i++)
    thresholds.push_back(MakeEncryptedKVBlob(key2, "r"));

  // Partition 3: 3 * target_batch_size (0 real, 15 padding)
  for (int i = 0; i < 15; i++)
    padding.push_back(MakeEncryptedKVBlob(key3, "p"));

  auto batches =
      RouteToScatterShuffle(verification_payload, target_batch_size, thresholds, padding);
  ASSERT_TRUE(batches.ok());
  // We expect 1 batch for P1, 2 batches for P2, 3 batches for P3 = 6 batches total.
  EXPECT_THAT(*batches, SizeIs(6));

  for (const auto& batch : *batches) {
    // Each batch should contain target_batch_size + 1 elements (1 config + N records)
    EXPECT_THAT(batch, SizeIs(target_batch_size + 1));
    EXPECT_EQ(batch[0].data, "payload");
    for (size_t i = 1; i <= target_batch_size; i++) {
      EXPECT_FALSE(batch[i].key.type_url().empty());
    }
  }
}

TEST(ScatterShuffleRouterTest, SplitsRemainderBalancedPartitionIntoActualBatchSizeBatches) {
  const EncryptedBlob verification_payload("payload");
  MetricEpochKey key = CreateMetricEpochKey({.project_id = 1001, .metric_id = 1, .epoch_index = 0});

  // 25 real messages + 2 padding messages = 27 messages with target_batch_size = 10
  // -> batch_num = 3, actual_batch_size = 9.
  std::vector<EncryptedKVBlob> real_msgs;
  for (int i = 0; i < 25; ++i) {
    real_msgs.push_back(MakeEncryptedKVBlob(key, "real"));
  }
  std::vector<EncryptedKVBlob> padding_msgs;
  for (int i = 0; i < 2; ++i) {
    padding_msgs.push_back(MakeEncryptedKVBlob(key, "pad"));
  }

  absl::StatusOr<std::vector<KVCollection>> batches = RouteToScatterShuffle(
      verification_payload, /*target_batch_size=*/10, real_msgs, padding_msgs);
  ASSERT_TRUE(batches.ok());
  ASSERT_THAT(*batches, SizeIs(3));
  for (const auto& batch : *batches) {
    EXPECT_THAT(batch, SizeIs(10));  // 1 config + 9 records (actual_batch_size)
  }
}

TEST(ScatterShuffleRouterTest, FailsWhenInputPartitionSizeIsNotAMultipleOfBatchNum) {
  const EncryptedBlob verification_payload("payload");
  MetricEpochKey key = CreateMetricEpochKey({.project_id = 1001, .metric_id = 1, .epoch_index = 0});

  // 3 messages with target_batch_size = 2 -> batch_num = 2, and 3 is not a multiple of 2.
  std::vector<EncryptedKVBlob> real_msgs = {
      MakeEncryptedKVBlob(key, "real_k1_1"),
      MakeEncryptedKVBlob(key, "real_k1_2"),
      MakeEncryptedKVBlob(key, "real_k1_3"),
  };

  absl::StatusOr<std::vector<KVCollection>> result =
      RouteToScatterShuffle(verification_payload, /*target_batch_size=*/2, real_msgs, {});

  EXPECT_THAT(result.status(), StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("multiple")));
}

TEST(ScatterShuffleRouterTest, RejectsAnyConfigurationWithATargetBatchSizeOfZero) {
  const EncryptedBlob verification_payload("payload");
  auto batches = RouteToScatterShuffle(verification_payload, /*target_batch_size=*/0, {}, {});
  EXPECT_THAT(batches.status(),
              StatusIs(absl::StatusCode::kInvalidArgument, HasSubstr("must be positive")));
}

TEST(ScatterShuffleRouterTest, UnderpaddedBatchRouterOutputsShortBatch) {
  const EncryptedBlob verification_payload("payload");
  MetricEpochKey key = CreateMetricEpochKey({.project_id = 1001, .metric_id = 1, .epoch_index = 0});

  std::vector<EncryptedKVBlob> real_msgs = {
      MakeEncryptedKVBlob(key, "real_k1_1"),
      MakeEncryptedKVBlob(key, "real_k1_2"),
  };

  auto batches = RouteUnderpaddedBatchToScatterShuffle(verification_payload,
                                                       /*target_batch_size=*/2, real_msgs, {});
  ASSERT_TRUE(batches.ok());
  ASSERT_THAT(*batches, SizeIs(1));
  EXPECT_THAT(batches->front(), SizeIs(2));  // Config + 1 msg, instead of Config + 2 msgs
}

TEST(ScatterShuffleRouterTest, LateVerificationPayloadRouterShiftsConfigToBack) {
  const EncryptedBlob verification_payload("payload");
  MetricEpochKey key = CreateMetricEpochKey({.project_id = 1001, .metric_id = 1, .epoch_index = 0});

  std::vector<EncryptedKVBlob> real_msgs = {
      MakeEncryptedKVBlob(key, "real_k1_1"),
      MakeEncryptedKVBlob(key, "real_k1_2"),
  };

  auto batches = RouteToScatterShuffleWithLateVerificationPayload(
      verification_payload, /*target_batch_size=*/2, real_msgs, {});
  ASSERT_TRUE(batches.ok());
  ASSERT_THAT(*batches, SizeIs(1));
  ASSERT_THAT(batches->front(), SizeIs(3));  // 2 msgs + Config
  EXPECT_EQ(batches->front().front().data,
            real_msgs.front().ToKV().data);            // verification payload is not first anymore
  EXPECT_EQ(batches->front().back().data, "payload");  // verification payload is moved to back
}

TEST(ScatterShuffleRouterTest, MixedPartitionBatchRouterSwapsElementsBetweenBatches) {
  const EncryptedBlob verification_payload("payload");
  MetricEpochKey key1 =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 1, .epoch_index = 0});
  MetricEpochKey key2 =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 2, .epoch_index = 0});

  std::vector<EncryptedKVBlob> real_msgs = {
      MakeEncryptedKVBlob(key1, "real_k1_1"),
      MakeEncryptedKVBlob(key1, "real_k1_2"),
      MakeEncryptedKVBlob(key2, "real_k2_1"),
      MakeEncryptedKVBlob(key2, "real_k2_2"),
  };

  auto normal_batches =
      RouteToScatterShuffle(verification_payload, /*target_batch_size=*/2, real_msgs, {});
  auto mixed_batches = RouteMixedPartitionBatchToScatterShuffle(
      verification_payload, /*target_batch_size=*/2, real_msgs, {});

  ASSERT_TRUE(normal_batches.ok());
  ASSERT_TRUE(mixed_batches.ok());
  ASSERT_THAT(*normal_batches, SizeIs(2));
  ASSERT_THAT(*mixed_batches, SizeIs(2));

  // They should be swapped
  EXPECT_EQ(mixed_batches->front().back().data, normal_batches->back().back().data);
  EXPECT_EQ(mixed_batches->back().back().data, normal_batches->front().back().data);
}

}  // namespace
}  // namespace cobalt::trusted_shuffler::testing::integration
