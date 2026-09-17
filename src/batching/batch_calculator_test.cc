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

#include <cstdint>
#include <string>

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
      .batch_num = 1,
      .padding_count = 50,
  };
  EXPECT_THAT(*params, Eq(expected));
}

TEST(BatchCalculatorTest, ComputeBatchParametersAddsPaddingWhenRemainderExists) {
  // 105 messages with target 100 => batch_num = 2.
  // remainder = 105 % 100 = 5.
  // padding = (100 - 5) % 100 = 95.
  // message_count + padding = 200 = 2 * 100.
  const auto params = ComputeForSinglePartition(/*message_count=*/105, /*target_batch_size=*/100);
  ASSERT_THAT(params, IsOk());

  const BatchParameters expected{
      .target_batch_size = 100,
      .batch_num = 2,
      .padding_count = 95,
  };
  EXPECT_THAT(*params, Eq(expected));
}

TEST(BatchCalculatorTest, ComputeBatchParametersMultipleBatchesWithRemainder) {
  // 150 messages with target 100 => batch_num = 2.
  // remainder = 150 % 100 = 50.
  // padding = (100 - 50) % 100 = 50.
  // message_count + padding = 200 = 2 * 100.
  const auto params = ComputeForSinglePartition(/*message_count=*/150, /*target_batch_size=*/100);
  ASSERT_THAT(params, IsOk());

  const BatchParameters expected{
      .target_batch_size = 100,
      .batch_num = 2,
      .padding_count = 50,
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
      .batch_num = 2,
      .padding_count = 49,
  };
  EXPECT_THAT(*params, Eq(expected));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
