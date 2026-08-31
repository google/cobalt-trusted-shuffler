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

#include "src/crypto/multiset_fingerprint.h"

#include <string>
#include <vector>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/run_context.pb.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;

constexpr Stage kStage1 = static_cast<Stage>(1);
constexpr Stage kStage2 = static_cast<Stage>(2);
constexpr Stage kStage3 = static_cast<Stage>(3);

MetricEpochKey CreateMetricEpochKey(int32_t project_id, int32_t metric_id, int64_t epoch_index) {
  MetricEpochKey key;
  key.set_project_id(project_id);
  key.set_metric_id(metric_id);
  key.set_epoch_index(epoch_index);
  return key;
}

TEST(MultisetFingerprintTest, InitAndBuild) {
  MultisetFingerprint fp(kStage1, /*is_partial=*/false, "run-12345");

  const RunFingerprint emitted = fp.Build();
  EXPECT_EQ(emitted.stage_id(), kStage1);
  EXPECT_FALSE(emitted.is_partial());
  EXPECT_EQ(emitted.run_id(), "run-12345");
  EXPECT_EQ(emitted.entries_size(), 0);
}

TEST(MultisetFingerprintTest, AddMessageTracksMessageCountsAndIgnoresPadding) {
  MultisetFingerprint fp(kStage1, true, "run-1");

  const MetricEpochKey key1 = CreateMetricEpochKey(10, 20, 100);
  const MetricEpochKey key2 = CreateMetricEpochKey(10, 20, 101);

  // Padding message should be ignored
  ShuffledMessage padding_msg;
  *padding_msg.mutable_metric_epoch_key() = key1;
  padding_msg.set_is_padding(true);
  fp.AddMessage(padding_msg);

  // Valid messages for key1
  ShuffledMessage msg1;
  *msg1.mutable_metric_epoch_key() = key1;
  msg1.set_is_padding(false);
  fp.AddMessage(msg1);
  fp.AddMessage(msg1);

  // Valid message for key2
  ShuffledMessage msg2;
  *msg2.mutable_metric_epoch_key() = key2;
  msg2.set_is_padding(false);
  fp.AddMessage(msg2);

  const RunFingerprint emitted = fp.Build();
  EXPECT_EQ(emitted.stage_id(), kStage1);
  EXPECT_TRUE(emitted.is_partial());
  ASSERT_EQ(emitted.entries_size(), 2);

  EXPECT_EQ(emitted.entries(0).metric_epoch_key().epoch_index(), 100);
  EXPECT_EQ(emitted.entries(0).message_count(), 2);

  EXPECT_EQ(emitted.entries(1).metric_epoch_key().epoch_index(), 101);
  EXPECT_EQ(emitted.entries(1).message_count(), 1);
}

TEST(MultisetFingerprintTest, MergeCombinesMessageCounts) {
  MultisetFingerprint fp(kStage2, /*is_partial=*/false, "run-abc");

  const MetricEpochKey key = CreateMetricEpochKey(1, 2, 3);

  RunFingerprint partial_fp1;
  partial_fp1.set_stage_id(kStage2);
  partial_fp1.set_is_partial(true);
  partial_fp1.set_run_id("run-abc");

  FingerprintEntry* e1 = partial_fp1.add_entries();
  *e1->mutable_metric_epoch_key() = key;
  e1->set_message_count(5);

  RunFingerprint partial_fp2;
  partial_fp2.set_stage_id(kStage2);
  partial_fp2.set_is_partial(true);
  partial_fp2.set_run_id("run-abc");

  FingerprintEntry* e2 = partial_fp2.add_entries();
  *e2->mutable_metric_epoch_key() = key;
  e2->set_message_count(3);

  EXPECT_THAT(fp.Merge(partial_fp1), IsOk());
  EXPECT_THAT(fp.Merge(partial_fp2), IsOk());

  const RunFingerprint canonical = fp.Build();
  ASSERT_EQ(canonical.entries_size(), 1);
  EXPECT_EQ(canonical.entries(0).message_count(), 8);
}

TEST(MultisetFingerprintTest, MergeFailsWhenSelfIsPartial) {
  MultisetFingerprint fp(kStage2, /*is_partial=*/true, "run-abc");

  RunFingerprint partial_fp;
  partial_fp.set_stage_id(kStage2);
  partial_fp.set_is_partial(true);

  const absl::Status status = fp.Merge(partial_fp);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(status.message(), ::testing::HasSubstr("merging is only supported for building a complete stage fingerprint"));
}

TEST(MultisetFingerprintTest, MergeFailsWhenStageIdMismatch) {
  MultisetFingerprint fp(kStage2, /*is_partial=*/false, "run-abc");

  RunFingerprint partial_fp;
  partial_fp.set_stage_id(kStage3);
  partial_fp.set_is_partial(true);

  const absl::Status status = fp.Merge(partial_fp);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(status.message(), ::testing::HasSubstr("attempting to merge stage 3 into stage 2"));
}

TEST(MultisetFingerprintTest, MergeFailsWhenInputNotPartial) {
  MultisetFingerprint fp(kStage2, /*is_partial=*/false, "run-abc");

  RunFingerprint non_partial_fp;
  non_partial_fp.set_stage_id(kStage2);
  non_partial_fp.set_is_partial(false);

  const absl::Status status = fp.Merge(non_partial_fp);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(status.message(), ::testing::HasSubstr("Only partial fingerprints produced by workers can be merged"));
}

TEST(MultisetFingerprintTest, VerifyCompleteReturnsTrueForMatchingComplete) {
  const MetricEpochKey key = CreateMetricEpochKey(5, 5, 5);

  ShuffledMessage msg;
  *msg.mutable_metric_epoch_key() = key;
  msg.set_is_padding(false);

  MultisetFingerprint fp(kStage3, /*is_partial=*/false, "run-77");
  fp.AddMessage(msg);

  const RunFingerprint complete_fp = fp.Build();

  EXPECT_TRUE(fp.VerifyComplete(complete_fp));
  EXPECT_TRUE(fp.VerifyCompleteForMetricEpoch(complete_fp, key));
}

TEST(MultisetFingerprintTest, VerifyCompleteAllowsDifferentStageIds) {
  const MetricEpochKey key = CreateMetricEpochKey(5, 5, 5);

  ShuffledMessage msg;
  *msg.mutable_metric_epoch_key() = key;
  msg.set_is_padding(false);

  // Self is at kStage2
  MultisetFingerprint fp(kStage2, /*is_partial=*/false, "run-77");
  fp.AddMessage(msg);

  // RunFingerprint produced by kStage1
  RunFingerprint complete_fp_from_stage1;
  complete_fp_from_stage1.set_stage_id(kStage1);
  complete_fp_from_stage1.set_is_partial(false);
  complete_fp_from_stage1.set_run_id("run-77");

  FingerprintEntry* entry = complete_fp_from_stage1.add_entries();
  *entry->mutable_metric_epoch_key() = key;
  entry->set_message_count(1);

  // Verification across different pipeline stages MUST succeed
  EXPECT_TRUE(fp.VerifyComplete(complete_fp_from_stage1));
  EXPECT_TRUE(fp.VerifyCompleteForMetricEpoch(complete_fp_from_stage1, key));
}

TEST(MultisetFingerprintTest, VerifyCompleteForMetricEpochAllowsOtherPartitionsInRunFingerprint) {
  const MetricEpochKey key1 = CreateMetricEpochKey(5, 5, 5);
  const MetricEpochKey key2 = CreateMetricEpochKey(5, 5, 6);

  ShuffledMessage msg1;
  *msg1.mutable_metric_epoch_key() = key1;
  msg1.set_is_padding(false);

  MultisetFingerprint single_partition_fp(kStage3, /*is_partial=*/false, "run-77");
  single_partition_fp.AddMessage(msg1);

  // RunFingerprint contains both key1 and key2
  RunFingerprint multi_partition_run_fp;
  multi_partition_run_fp.set_stage_id(kStage3);
  multi_partition_run_fp.set_is_partial(false);
  multi_partition_run_fp.set_run_id("run-77");

  FingerprintEntry* e1 = multi_partition_run_fp.add_entries();
  *e1->mutable_metric_epoch_key() = key1;
  e1->set_message_count(1);

  FingerprintEntry* e2 = multi_partition_run_fp.add_entries();
  *e2->mutable_metric_epoch_key() = key2;
  e2->set_message_count(10);

  // VerifyCompleteForMetricEpoch should succeed for key1 because single_partition_fp only contains key1
  EXPECT_TRUE(single_partition_fp.VerifyCompleteForMetricEpoch(multi_partition_run_fp, key1));

  // VerifyCompleteForMetricEpoch for key2 should fail because single_partition_fp contains key1 instead of key2
  EXPECT_FALSE(single_partition_fp.VerifyCompleteForMetricEpoch(multi_partition_run_fp, key2));
}

TEST(MultisetFingerprintTest, VerifyCompleteForMetricEpochFailsIfMultisetHasMultiplePartitions) {
  const MetricEpochKey key1 = CreateMetricEpochKey(5, 5, 5);
  const MetricEpochKey key2 = CreateMetricEpochKey(5, 5, 6);

  ShuffledMessage msg1;
  *msg1.mutable_metric_epoch_key() = key1;
  msg1.set_is_padding(false);

  ShuffledMessage msg2;
  *msg2.mutable_metric_epoch_key() = key2;
  msg2.set_is_padding(false);

  MultisetFingerprint multi_fp(kStage3, /*is_partial=*/false, "run-77");
  multi_fp.AddMessage(msg1);
  multi_fp.AddMessage(msg2);

  const RunFingerprint complete_fp = multi_fp.Build();

  // VerifyCompleteForMetricEpoch must fail when MultisetFingerprint contains multiple partitions
  EXPECT_FALSE(multi_fp.VerifyCompleteForMetricEpoch(complete_fp, key1));
  EXPECT_FALSE(multi_fp.VerifyCompleteForMetricEpoch(complete_fp, key2));
}

TEST(MultisetFingerprintTest, VerifyCompleteReturnsFalseOnCountMismatch) {
  const MetricEpochKey key = CreateMetricEpochKey(5, 5, 5);

  ShuffledMessage msg;
  *msg.mutable_metric_epoch_key() = key;
  msg.set_is_padding(false);

  MultisetFingerprint fp(kStage3, /*is_partial=*/false, "run-77");
  fp.AddMessage(msg);

  RunFingerprint complete_fp = fp.Build();
  ASSERT_EQ(complete_fp.entries_size(), 1);
  complete_fp.mutable_entries(0)->set_message_count(999);

  EXPECT_FALSE(fp.VerifyComplete(complete_fp));
  EXPECT_FALSE(fp.VerifyCompleteForMetricEpoch(complete_fp, key));
}

TEST(MultisetFingerprintTest, VerifyCompleteReturnsFalseOnMismatch) {
  MultisetFingerprint fp(kStage3, /*is_partial=*/false, "run-77");

  RunFingerprint mismatch_run_id;
  mismatch_run_id.set_stage_id(kStage3);
  mismatch_run_id.set_is_partial(false);
  mismatch_run_id.set_run_id("different-run-id");
  EXPECT_FALSE(fp.VerifyComplete(mismatch_run_id));

  RunFingerprint partial_input;
  partial_input.set_stage_id(kStage3);
  partial_input.set_is_partial(true);
  partial_input.set_run_id("run-77");
  EXPECT_FALSE(fp.VerifyComplete(partial_input));

  MultisetFingerprint partial_fp(kStage3, true, "run-77");
  EXPECT_FALSE(partial_fp.VerifyComplete(mismatch_run_id));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
