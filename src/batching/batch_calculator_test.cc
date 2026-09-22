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

#include "src/batching/batch_calculator.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/crypto/release_encrypter.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::Eq;

MetricEpochKey CreateTestPartitionKey(int32_t project_id, int32_t metric_id, int64_t epoch_index) {
  MetricEpochKey key;
  key.set_project_id(project_id);
  key.set_metric_id(metric_id);
  key.set_epoch_index(epoch_index);
  return key;
}

struct PartitionCountSpec {
  MetricEpochKey key;
  int64_t message_count;
};

VerifiedIngestionVerificationResult CreateTestVerifiedResult(
    int32_t target_batch_size, absl::Span<const PartitionCountSpec> partition_specs) {
  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ABSL_CHECK_OK(release_key.status());

  VerifiedIngestionVerificationResult verified;
  verified.run_context.set_run_id(std::string(32, 'r'));
  verified.run_context.set_run_hash_key(std::string(32, 'k'));
  verified.run_context.set_release_key(*release_key);
  verified.run_context.mutable_run_config()->set_target_batch_size(target_batch_size);

  verified.canonical_fingerprint.set_run_id(std::string(32, 'r'));
  verified.canonical_fingerprint.set_stage_id(Stage::STAGE_INGESTION);
  verified.canonical_fingerprint.set_is_partial(false);

  for (const auto& spec : partition_specs) {
    verified.approved_partitions.insert(spec.key);

    FingerprintEntry* const entry = verified.canonical_fingerprint.add_entries();
    *entry->mutable_metric_epoch_key() = spec.key;
    entry->set_message_count(spec.message_count);
    entry->set_fingerprint(std::string(32, 'f'));
  }

  return verified;
}

absl::StatusOr<BatchParameters> ComputeForSinglePartition(int64_t message_count,
                                                          int32_t target_batch_size) {
  const MetricEpochKey key = CreateTestPartitionKey(1, 10, 100);
  const VerifiedIngestionVerificationResult result =
      CreateTestVerifiedResult(target_batch_size, {{.key = key, .message_count = message_count}});
  return ComputeBatchParametersForPartition(result, key);
}

TEST(BatchCalculatorTest, ComputeBatchParametersRejectsNonPositiveTargetBatchSize) {
  EXPECT_THAT(ComputeForSinglePartition(/*message_count=*/100, /*target_batch_size=*/0),
              StatusIs(absl::StatusCode::kInvalidArgument, "target_batch_size must be positive."));
  EXPECT_THAT(ComputeForSinglePartition(/*message_count=*/100, /*target_batch_size=*/-10),
              StatusIs(absl::StatusCode::kInvalidArgument, "target_batch_size must be positive."));
}

TEST(BatchCalculatorTest, ComputeBatchParametersRejectsNonPositiveMessageCount) {
  EXPECT_THAT(ComputeForSinglePartition(/*message_count=*/0, /*target_batch_size=*/100),
              StatusIs(absl::StatusCode::kInvalidArgument, "message_count must be positive."));
  EXPECT_THAT(ComputeForSinglePartition(/*message_count=*/-1, /*target_batch_size=*/100),
              StatusIs(absl::StatusCode::kInvalidArgument, "message_count must be positive."));
}

TEST(BatchCalculatorTest, ComputeBatchParametersRejectsBatchNumExceedingInt32Max) {
  EXPECT_THAT(ComputeForSinglePartition(/*message_count=*/3'000'000'000LL, /*target_batch_size=*/1),
              StatusIs(absl::StatusCode::kOutOfRange, "Computed batch_num exceeds int32_t max."));
}

TEST(BatchCalculatorTest, ComputeBatchParametersExactMultipleTargetBatchSize) {
  const auto params = ComputeForSinglePartition(/*message_count=*/200, /*target_batch_size=*/100);
  ASSERT_THAT(params, IsOk());

  const BatchParameters expected{
      .target_batch_size = 100,
      .actual_batch_size = 100,
      .batch_num = 2,
      .padding_count = 0,
  };
  EXPECT_THAT(*params, Eq(expected));
}

TEST(BatchCalculatorTest, ComputeBatchParametersSingleBatchWithoutRemainder) {
  const auto params = ComputeForSinglePartition(/*message_count=*/100, /*target_batch_size=*/100);
  ASSERT_THAT(params, IsOk());

  const BatchParameters expected{
      .target_batch_size = 100,
      .actual_batch_size = 100,
      .batch_num = 1,
      .padding_count = 0,
  };
  EXPECT_THAT(*params, Eq(expected));
}

TEST(BatchCalculatorTest, ComputeBatchParametersSingleBatchLessThanTarget) {
  const auto params = ComputeForSinglePartition(/*message_count=*/50, /*target_batch_size=*/100);
  ASSERT_THAT(params, IsOk());

  const BatchParameters expected{
      .target_batch_size = 100,
      .actual_batch_size = 50,
      .batch_num = 1,
      .padding_count = 0,
  };
  EXPECT_THAT(*params, Eq(expected));
}

TEST(BatchCalculatorTest, ComputeBatchParametersAddsPaddingWhenRemainderExists) {
  // 105 messages with target 100 => batch_num = 2.
  // remainder = 105 % 2 = 1.
  // padding = 2 - 1 = 1.
  // actual_batch_size = (105 + 1) / 2 = 53.
  // message_count + padding = 106 = 2 * 53.
  const auto params = ComputeForSinglePartition(/*message_count=*/105, /*target_batch_size=*/100);
  ASSERT_THAT(params, IsOk());

  const BatchParameters expected{
      .target_batch_size = 100,
      .actual_batch_size = 53,
      .batch_num = 2,
      .padding_count = 1,
  };
  EXPECT_THAT(*params, Eq(expected));
}

TEST(BatchCalculatorTest, ComputeBatchParametersMultipleBatchesWithRemainder) {
  // 25 messages with target 10 => batch_num = 3.
  // remainder = 25 % 3 = 1.
  // padding = 3 - 1 = 2.
  // actual_batch_size = (25 + 2) / 3 = 9.
  // message_count + padding = 27 = 3 * 9.
  const auto params = ComputeForSinglePartition(/*message_count=*/25, /*target_batch_size=*/10);
  ASSERT_THAT(params, IsOk());

  const BatchParameters expected{
      .target_batch_size = 10,
      .actual_batch_size = 9,
      .batch_num = 3,
      .padding_count = 2,
  };
  EXPECT_THAT(*params, Eq(expected));
}

TEST(BatchCalculatorTest, ComputeBatchParametersMultipleBatchesWithoutRemainder) {
  // 150 messages with target 100 => batch_num = 2.
  // remainder = 150 % 2 = 0.
  // padding = 0.
  // actual_batch_size = 150 / 2 = 75.
  const auto params = ComputeForSinglePartition(/*message_count=*/150, /*target_batch_size=*/100);
  ASSERT_THAT(params, IsOk());

  const BatchParameters expected{
      .target_batch_size = 100,
      .actual_batch_size = 75,
      .batch_num = 2,
      .padding_count = 0,
  };
  EXPECT_THAT(*params, Eq(expected));
}

TEST(BatchCalculatorTest, ComputeBatchParametersForPartitionFailsWhenNotApproved) {
  const MetricEpochKey approved_key = CreateTestPartitionKey(1, 10, 100);
  const MetricEpochKey unapproved_key = CreateTestPartitionKey(2, 20, 200);

  const VerifiedIngestionVerificationResult result = CreateTestVerifiedResult(
      /*target_batch_size=*/100, {{.key = approved_key, .message_count = 150}});

  EXPECT_THAT(ComputeBatchParametersForPartition(result, unapproved_key),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "Partition is not approved in ThresholdReport."));
}

TEST(BatchCalculatorTest, ComputeBatchParametersForPartitionFailsWhenFingerprintEntryMissing) {
  const MetricEpochKey approved_key = CreateTestPartitionKey(1, 10, 100);

  VerifiedIngestionVerificationResult result =
      CreateTestVerifiedResult(/*target_batch_size=*/100, {});
  result.approved_partitions.insert(approved_key);
  // Canonical fingerprint has no entries.

  EXPECT_THAT(ComputeBatchParametersForPartition(result, approved_key),
              StatusIs(absl::StatusCode::kNotFound,
                       "No matching entry found in canonical fingerprint for approved partition."));
}

TEST(BatchCalculatorTest, ComputeBatchParametersForPartitionSucceedsForApprovedPartition) {
  const MetricEpochKey key = CreateTestPartitionKey(1, 10, 100);
  const VerifiedIngestionVerificationResult result = CreateTestVerifiedResult(
      /*target_batch_size=*/100, {{.key = key, .message_count = 151}});

  const auto params = ComputeBatchParametersForPartition(result, key);
  ASSERT_THAT(params, IsOk());

  const BatchParameters expected{
      .target_batch_size = 100,
      .actual_batch_size = 76,
      .batch_num = 2,
      .padding_count = 1,
  };
  EXPECT_THAT(*params, Eq(expected));
}

TEST(BatchCalculatorTest, ComputeExpectedGatherGroupSizeRejectsInvalidGroupId) {
  const MetricEpochKey key = CreateTestPartitionKey(1, 10, 100);
  const VerifiedIngestionVerificationResult result = CreateTestVerifiedResult(
      /*target_batch_size=*/100, {{.key = key, .message_count = 150}});

  // batch_num = 2, valid group_id in [0, 2).
  EXPECT_THAT(ComputeExpectedGatherGroupSizeForPartition(result, key, /*group_id=*/-1),
              StatusIs(absl::StatusCode::kInvalidArgument, "group_id must be in [0, batch_num)."));
  EXPECT_THAT(ComputeExpectedGatherGroupSizeForPartition(result, key, /*group_id=*/2),
              StatusIs(absl::StatusCode::kInvalidArgument, "group_id must be in [0, batch_num)."));
}

TEST(BatchCalculatorTest, ComputeExpectedGatherGroupSizeEvenlyDivisibleBatchSize) {
  const MetricEpochKey key = CreateTestPartitionKey(1, 10, 100);
  // 150 messages, target_batch_size 100 -> batch_num = 2, padding = 0, actual_batch_size = 75.
  // 75 % 2 = 1.
  // Group 0 receives: 2 * (75/2 + 1) = 2 * 38 = 76.
  // Group 1 receives: 2 * (75/2 + 0) = 2 * 37 = 74.
  // Total = 76 + 74 = 150.
  const VerifiedIngestionVerificationResult result = CreateTestVerifiedResult(
      /*target_batch_size=*/100, {{.key = key, .message_count = 150}});

  EXPECT_THAT(ComputeExpectedGatherGroupSizeForPartition(result, key, /*group_id=*/0),
              absl_testing::IsOkAndHolds(76));
  EXPECT_THAT(ComputeExpectedGatherGroupSizeForPartition(result, key, /*group_id=*/1),
              absl_testing::IsOkAndHolds(74));
}

TEST(BatchCalculatorTest, ComputeExpectedGatherGroupSizeLargeBatchCountRegime) {
  const MetricEpochKey key = CreateTestPartitionKey(1, 10, 100);
  // 10 messages, target_batch_size 4 -> batch_num = 3, padding = 2, actual_batch_size = 4.
  // 4 % 3 = 1.
  // Group 0 receives: 3 * (4/3 + 1) = 3 * 2 = 6.
  // Group 1 receives: 3 * (4/3 + 0) = 3 * 1 = 3.
  // Group 2 receives: 3 * (4/3 + 0) = 3 * 1 = 3.
  // Total = 6 + 3 + 3 = 12 = 10 + 2.
  const VerifiedIngestionVerificationResult result = CreateTestVerifiedResult(
      /*target_batch_size=*/4, {{.key = key, .message_count = 10}});

  EXPECT_THAT(ComputeExpectedGatherGroupSizeForPartition(result, key, /*group_id=*/0),
              absl_testing::IsOkAndHolds(6));
  EXPECT_THAT(ComputeExpectedGatherGroupSizeForPartition(result, key, /*group_id=*/1),
              absl_testing::IsOkAndHolds(3));
  EXPECT_THAT(ComputeExpectedGatherGroupSizeForPartition(result, key, /*group_id=*/2),
              absl_testing::IsOkAndHolds(3));
}

TEST(BatchCalculatorTest, GatherGroupIdForPositionCyclesThroughTheGroups) {
  EXPECT_THAT(GatherGroupIdForPosition(/*position=*/0, /*batch_num=*/3), Eq(0));
  EXPECT_THAT(GatherGroupIdForPosition(/*position=*/1, /*batch_num=*/3), Eq(1));
  EXPECT_THAT(GatherGroupIdForPosition(/*position=*/2, /*batch_num=*/3), Eq(2));
  EXPECT_THAT(GatherGroupIdForPosition(/*position=*/3, /*batch_num=*/3), Eq(0));
  EXPECT_THAT(GatherGroupIdForPosition(/*position=*/4, /*batch_num=*/3), Eq(1));
}

TEST(BatchCalculatorTest, GatherGroupIdForPositionSendsEveryMessageToTheOnlyGroup) {
  for (int64_t position = 0; position < 5; ++position) {
    EXPECT_THAT(GatherGroupIdForPosition(position, /*batch_num=*/1), Eq(0));
  }
}

TEST(BatchCalculatorTest, GatherGroupIdForPositionSpreadsABatchEvenlyAcrossGroups) {
  // Every gather worker must receive a balanced share of each scatter worker's
  // batch: no group may be owed more than one message more than another, and no
  // message may be assigned to a group that does not exist.
  for (const int32_t batch_num : {1, 2, 3, 7, 16}) {
    for (const int32_t actual_batch_size : {1, 2, 5, 16, 100}) {
      std::vector<int32_t> assigned_per_group(batch_num, 0);
      for (int64_t position = 0; position < actual_batch_size; ++position) {
        const int32_t group_id = GatherGroupIdForPosition(position, batch_num);
        ASSERT_GE(group_id, 0);
        ASSERT_LT(group_id, batch_num);
        ++assigned_per_group[group_id];
      }

      const auto [fewest, most] =
          std::minmax_element(assigned_per_group.begin(), assigned_per_group.end());
      EXPECT_LE(*most - *fewest, 1)
          << "batch_num=" << batch_num << " actual_batch_size=" << actual_batch_size;
    }
  }
}

TEST(BatchCalculatorTest, ExpectedGatherGroupSizeMatchesScatterGroupAssignment) {
  // The closed form in ComputeExpectedGatherGroupSizeForPartition counts the
  // messages GatherGroupIdForPosition hands each group. Rebuild that count from
  // the assignment itself so the two cannot drift: changing how ScatterShuffle
  // labels messages without reworking the count fails here.
  struct PartitionSpec {
    int32_t target_batch_size;
    int64_t message_count;
  };
  const PartitionSpec partition_specs[] = {
      {.target_batch_size = 1, .message_count = 1},
      {.target_batch_size = 100, .message_count = 150},
      {.target_batch_size = 4, .message_count = 10},
      {.target_batch_size = 10, .message_count = 10},
      {.target_batch_size = 7, .message_count = 100},
      {.target_batch_size = 1, .message_count = 9},
      {.target_batch_size = 64, .message_count = 1000},
  };

  for (const PartitionSpec& spec : partition_specs) {
    const MetricEpochKey key = CreateTestPartitionKey(1, 10, 100);
    const VerifiedIngestionVerificationResult result = CreateTestVerifiedResult(
        spec.target_batch_size, {{.key = key, .message_count = spec.message_count}});
    const absl::StatusOr<BatchParameters> params = ComputeBatchParametersForPartition(result, key);
    ASSERT_THAT(params, IsOk());

    // Every scatter batch holds actual_batch_size messages and labels them by
    // position, so replaying one batch's assignment and scaling by batch_num
    // gives each gather group's size.
    std::vector<int32_t> assigned_per_group(params->batch_num, 0);
    for (int64_t position = 0; position < params->actual_batch_size; ++position) {
      ++assigned_per_group[GatherGroupIdForPosition(position, params->batch_num)];
    }

    int64_t total_gathered = 0;
    for (int32_t group_id = 0; group_id < params->batch_num; ++group_id) {
      const int32_t expected_group_size = params->batch_num * assigned_per_group[group_id];
      EXPECT_THAT(ComputeExpectedGatherGroupSizeForPartition(result, key, group_id),
                  absl_testing::IsOkAndHolds(expected_group_size))
          << "target_batch_size=" << spec.target_batch_size
          << " message_count=" << spec.message_count << " group_id=" << group_id;
      total_gathered += expected_group_size;
    }

    // The gather groups together hold the padded partition, so no message is
    // stranded in a group nobody expects.
    EXPECT_THAT(total_gathered, Eq(spec.message_count + params->padding_count))
        << "target_batch_size=" << spec.target_batch_size
        << " message_count=" << spec.message_count;
  }
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
