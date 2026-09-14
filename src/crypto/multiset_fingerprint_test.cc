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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "absl/status/status_matchers.h"
#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "google/protobuf/util/json_util.h"
#include "gtest/gtest.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/multiset_fingerprint_golden_test_vectors.pb.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::cobalt::trusted_shuffler::testing::MultisetFingerprintGoldenTestVectors;

using ::absl_testing::IsOk;

constexpr Stage kStage1 = static_cast<Stage>(1);
constexpr Stage kStage2 = static_cast<Stage>(2);
constexpr Stage kStage3 = static_cast<Stage>(3);

constexpr absl::string_view kTestRunHashKey = "test-run-hash-key-32-bytes-long";
constexpr std::size_t kFingerprintSize = MultisetFingerprint::kFingerprintSize;

MetricEpochKey CreateMetricEpochKey(int32_t project_id, int32_t metric_id, int64_t epoch_index) {
  MetricEpochKey key;
  key.set_project_id(project_id);
  key.set_metric_id(metric_id);
  key.set_epoch_index(epoch_index);
  return key;
}

RunContext CreateTestRunContext(absl::string_view run_id, absl::string_view run_hash_key) {
  RunContext context;
  context.set_run_id(std::string(run_id));
  context.set_run_hash_key(std::string(run_hash_key));
  return context;
}

TEST(MultisetFingerprintTest, InitAndBuild) {
  const RunContext run_context = CreateTestRunContext("run-12345", kTestRunHashKey);
  MultisetFingerprint fp(kStage1, /*is_partial=*/false, run_context, kFingerprintSize);

  const RunFingerprint emitted = fp.Build();
  EXPECT_EQ(emitted.stage_id(), kStage1);
  EXPECT_FALSE(emitted.is_partial());
  EXPECT_EQ(emitted.run_id(), "run-12345");
  EXPECT_EQ(emitted.entries_size(), 0);
}

TEST(MultisetFingerprintTest, AddMessageTracksMessageCountsAndIgnoresPadding) {
  const RunContext run_context = CreateTestRunContext("run-1", kTestRunHashKey);
  MultisetFingerprint fp(kStage1, /*is_partial=*/true, run_context, kFingerprintSize);

  const MetricEpochKey key1 = CreateMetricEpochKey(10, 20, 100);
  const MetricEpochKey key2 = CreateMetricEpochKey(10, 20, 101);

  // Padding message should be ignored
  ShuffledMessage padding_msg;
  *padding_msg.mutable_metric_epoch_key() = key1;
  padding_msg.set_is_padding(true);
  padding_msg.set_customer_message("padding_payload");
  fp.AddMessage(padding_msg);

  // Valid messages for key1
  ShuffledMessage msg1;
  *msg1.mutable_metric_epoch_key() = key1;
  msg1.set_is_padding(false);
  msg1.set_customer_message("payload_1");
  fp.AddMessage(msg1);
  fp.AddMessage(msg1);

  // Valid message for key2
  ShuffledMessage msg2;
  *msg2.mutable_metric_epoch_key() = key2;
  msg2.set_is_padding(false);
  msg2.set_customer_message("payload_2");
  fp.AddMessage(msg2);

  const RunFingerprint emitted = fp.Build();
  EXPECT_EQ(emitted.stage_id(), kStage1);
  EXPECT_TRUE(emitted.is_partial());
  ASSERT_EQ(emitted.entries_size(), 2);
  const FingerprintEntry& entry0 = emitted.entries(0);
  const FingerprintEntry& entry1 = emitted.entries(1);
  const FingerprintEntry& e_100 =
      (entry0.metric_epoch_key().epoch_index() == 100) ? entry0 : entry1;
  const FingerprintEntry& e_101 =
      (entry0.metric_epoch_key().epoch_index() == 101) ? entry0 : entry1;

  EXPECT_EQ(e_100.metric_epoch_key().epoch_index(), 100);
  EXPECT_EQ(e_100.message_count(), 2);
  EXPECT_EQ(e_100.fingerprint().size(), MultisetFingerprint::kFingerprintSize);

  EXPECT_EQ(e_101.metric_epoch_key().epoch_index(), 101);
  EXPECT_EQ(e_101.message_count(), 1);
  EXPECT_EQ(e_101.fingerprint().size(), MultisetFingerprint::kFingerprintSize);
}

TEST(MultisetFingerprintTest, MergeCombinesMessageCountsAndFingerprints) {
  const RunContext run_context = CreateTestRunContext("run-abc", kTestRunHashKey);
  MultisetFingerprint fp(kStage2, /*is_partial=*/false, run_context, kFingerprintSize);

  const MetricEpochKey key = CreateMetricEpochKey(1, 2, 3);
  ShuffledMessage msg;
  *msg.mutable_metric_epoch_key() = key;
  msg.set_is_padding(false);
  msg.set_customer_message("test_payload");

  MultisetFingerprint partial1(kStage2, /*is_partial=*/true, run_context, kFingerprintSize);
  for (int i = 0; i < 5; ++i) {
    partial1.AddMessage(msg);
  }
  const RunFingerprint partial_fp1 = partial1.Build();

  MultisetFingerprint partial2(kStage2, /*is_partial=*/true, run_context, kFingerprintSize);
  for (int i = 0; i < 3; ++i) {
    partial2.AddMessage(msg);
  }
  const RunFingerprint partial_fp2 = partial2.Build();

  EXPECT_THAT(fp.Merge(partial_fp1), IsOk());
  EXPECT_THAT(fp.Merge(partial_fp2), IsOk());

  const RunFingerprint canonical = fp.Build();
  ASSERT_EQ(canonical.entries_size(), 1);
  EXPECT_EQ(canonical.entries(0).message_count(), 8);
  EXPECT_EQ(canonical.entries(0).fingerprint().size(), MultisetFingerprint::kFingerprintSize);

  MultisetFingerprint direct(kStage2, /*is_partial=*/false, run_context, kFingerprintSize);
  for (int i = 0; i < 8; ++i) {
    direct.AddMessage(msg);
  }
  EXPECT_EQ(canonical.entries(0).fingerprint(), direct.Build().entries(0).fingerprint());
  EXPECT_TRUE(direct.VerifyComplete(canonical));
}

TEST(MultisetFingerprintTest, MergeFailsWhenSelfIsPartial) {
  const RunContext run_context = CreateTestRunContext("run-abc", kTestRunHashKey);
  MultisetFingerprint fp(kStage2, /*is_partial=*/true, run_context, kFingerprintSize);

  RunFingerprint partial_fp;
  partial_fp.set_stage_id(kStage2);
  partial_fp.set_is_partial(true);

  const absl::Status status = fp.Merge(partial_fp);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_THAT(
      status.message(),
      ::testing::HasSubstr("merging is only supported for building a complete stage fingerprint"));
}

TEST(MultisetFingerprintTest, MergeFailsWhenRunIdMismatch) {
  const RunContext run_context = CreateTestRunContext("run-abc", kTestRunHashKey);
  MultisetFingerprint fp(kStage2, /*is_partial=*/false, run_context, kFingerprintSize);

  RunFingerprint partial_fp;
  partial_fp.set_stage_id(kStage2);
  partial_fp.set_is_partial(true);
  partial_fp.set_run_id("different-run-id");

  const absl::Status status = fp.Merge(partial_fp);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(status.message(),
              ::testing::HasSubstr("Cannot merge fingerprints from different run IDs."));
  EXPECT_THAT(status.message(), ::testing::Not(::testing::HasSubstr("different-run-id")));
  EXPECT_THAT(status.message(), ::testing::Not(::testing::HasSubstr("run-abc")));
}

TEST(MultisetFingerprintTest, MergeFailsWhenStageIdMismatch) {
  const RunContext run_context = CreateTestRunContext("run-abc", kTestRunHashKey);
  MultisetFingerprint fp(kStage2, /*is_partial=*/false, run_context, kFingerprintSize);

  RunFingerprint partial_fp;
  partial_fp.set_stage_id(kStage3);
  partial_fp.set_is_partial(true);
  partial_fp.set_run_id("run-abc");

  const absl::Status status = fp.Merge(partial_fp);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(status.message(), ::testing::HasSubstr("attempting to merge stage 3 into stage 2"));
}

TEST(MultisetFingerprintTest, MergeFailsWhenInputNotPartial) {
  const RunContext run_context = CreateTestRunContext("run-abc", kTestRunHashKey);
  MultisetFingerprint fp(kStage2, /*is_partial=*/false, run_context, kFingerprintSize);

  RunFingerprint non_partial_fp;
  non_partial_fp.set_stage_id(kStage2);
  non_partial_fp.set_is_partial(false);
  non_partial_fp.set_run_id("run-abc");

  const absl::Status status = fp.Merge(non_partial_fp);
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(status.message(),
              ::testing::HasSubstr("Only partial fingerprints produced by workers can be merged"));
}

TEST(MultisetFingerprintTest, VerifyCompleteReturnsTrueForMatchingComplete) {
  const MetricEpochKey key = CreateMetricEpochKey(5, 5, 5);

  ShuffledMessage msg;
  *msg.mutable_metric_epoch_key() = key;
  msg.set_is_padding(false);
  msg.set_customer_message("message_payload");

  const RunContext run_context = CreateTestRunContext("run-77", kTestRunHashKey);
  MultisetFingerprint fp(kStage3, /*is_partial=*/false, run_context, kFingerprintSize);
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
  msg.set_customer_message("stage_transfer_message");

  const RunContext run_context = CreateTestRunContext("run-77", kTestRunHashKey);
  // Self is at kStage2
  MultisetFingerprint fp(kStage2, /*is_partial=*/false, run_context, kFingerprintSize);
  fp.AddMessage(msg);

  // RunFingerprint produced by kStage1
  MultisetFingerprint stage1_fp(kStage1, /*is_partial=*/false, run_context, kFingerprintSize);
  stage1_fp.AddMessage(msg);
  const RunFingerprint complete_fp_from_stage1 = stage1_fp.Build();

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
  msg1.set_customer_message("payload1");

  ShuffledMessage msg2;
  *msg2.mutable_metric_epoch_key() = key2;
  msg2.set_is_padding(false);
  msg2.set_customer_message("payload2");

  const RunContext run_context = CreateTestRunContext("run-77", kTestRunHashKey);
  MultisetFingerprint single_partition_fp(kStage3, /*is_partial=*/false, run_context,
                                          kFingerprintSize);
  single_partition_fp.AddMessage(msg1);

  // RunFingerprint contains both key1 and key2
  MultisetFingerprint multi_fp(kStage3, /*is_partial=*/false, run_context, kFingerprintSize);
  multi_fp.AddMessage(msg1);
  for (int i = 0; i < 10; ++i) {
    multi_fp.AddMessage(msg2);
  }
  const RunFingerprint multi_partition_run_fp = multi_fp.Build();

  // VerifyCompleteForMetricEpoch should succeed for key1 because single_partition_fp only contains
  // key1
  EXPECT_TRUE(single_partition_fp.VerifyCompleteForMetricEpoch(multi_partition_run_fp, key1));

  // VerifyCompleteForMetricEpoch for key2 should fail because single_partition_fp contains key1
  // instead of key2
  EXPECT_FALSE(single_partition_fp.VerifyCompleteForMetricEpoch(multi_partition_run_fp, key2));
}

TEST(MultisetFingerprintTest, VerifyCompleteForMetricEpochFailsIfMultisetHasMultiplePartitions) {
  const MetricEpochKey key1 = CreateMetricEpochKey(5, 5, 5);
  const MetricEpochKey key2 = CreateMetricEpochKey(5, 5, 6);

  ShuffledMessage msg1;
  *msg1.mutable_metric_epoch_key() = key1;
  msg1.set_is_padding(false);
  msg1.set_customer_message("payload1");

  ShuffledMessage msg2;
  *msg2.mutable_metric_epoch_key() = key2;
  msg2.set_is_padding(false);
  msg2.set_customer_message("payload2");

  const RunContext run_context = CreateTestRunContext("run-77", kTestRunHashKey);
  MultisetFingerprint multi_fp(kStage3, /*is_partial=*/false, run_context, kFingerprintSize);
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
  msg.set_customer_message("payload");

  const RunContext run_context = CreateTestRunContext("run-77", kTestRunHashKey);
  MultisetFingerprint fp(kStage3, /*is_partial=*/false, run_context, kFingerprintSize);
  fp.AddMessage(msg);

  RunFingerprint complete_fp = fp.Build();
  ASSERT_EQ(complete_fp.entries_size(), 1);
  complete_fp.mutable_entries(0)->set_message_count(999);

  EXPECT_FALSE(fp.VerifyComplete(complete_fp));
  EXPECT_FALSE(fp.VerifyCompleteForMetricEpoch(complete_fp, key));
}

TEST(MultisetFingerprintTest, VerifyCompleteReturnsFalseOnMismatch) {
  const RunContext run_context = CreateTestRunContext("run-77", kTestRunHashKey);
  MultisetFingerprint fp(kStage3, /*is_partial=*/false, run_context, kFingerprintSize);

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

  MultisetFingerprint partial_fp(kStage3, true, run_context, kFingerprintSize);
  EXPECT_FALSE(partial_fp.VerifyComplete(mismatch_run_id));
}

TEST(MultisetFingerprintTest, VerifyCompleteFailsWhenKeysDoNotOverlap) {
  const RunContext run_context = CreateTestRunContext("run-77", kTestRunHashKey);
  const MetricEpochKey key1 = CreateMetricEpochKey(1, 1, 1);
  const MetricEpochKey key2 = CreateMetricEpochKey(2, 2, 2);

  ShuffledMessage msg1;
  *msg1.mutable_metric_epoch_key() = key1;
  msg1.set_customer_message("msg1");

  ShuffledMessage msg2;
  *msg2.mutable_metric_epoch_key() = key2;
  msg2.set_customer_message("msg2");

  MultisetFingerprint fp1(kStage1, /*is_partial=*/false, run_context, kFingerprintSize);
  fp1.AddMessage(msg1);

  MultisetFingerprint fp2(kStage1, /*is_partial=*/false, run_context, kFingerprintSize);
  fp2.AddMessage(msg2);

  MultisetFingerprint fp_both(kStage1, /*is_partial=*/false, run_context, kFingerprintSize);
  fp_both.AddMessage(msg1);
  fp_both.AddMessage(msg2);

  const RunFingerprint rf1 = fp1.Build();
  const RunFingerprint rf2 = fp2.Build();
  const RunFingerprint rf_both = fp_both.Build();

  // Disjoint keys
  EXPECT_FALSE(fp1.VerifyComplete(rf2));
  EXPECT_FALSE(fp2.VerifyComplete(rf1));

  // Key subset / superset
  EXPECT_FALSE(fp1.VerifyComplete(rf_both));
  EXPECT_FALSE(fp_both.VerifyComplete(rf1));
}

TEST(MultisetFingerprintTest, MultisetOrderIndependence) {
  const MetricEpochKey key = CreateMetricEpochKey(1, 2, 3);

  ShuffledMessage msg_a;
  *msg_a.mutable_metric_epoch_key() = key;
  msg_a.set_is_padding(false);
  msg_a.set_customer_message("customer_payload_A");

  ShuffledMessage msg_b;
  *msg_b.mutable_metric_epoch_key() = key;
  msg_b.set_is_padding(false);
  msg_b.set_customer_message("customer_payload_B");

  const RunContext run_context = CreateTestRunContext("run-order", kTestRunHashKey);
  // fp1 processes A then B
  MultisetFingerprint fp1(kStage1, /*is_partial=*/false, run_context, kFingerprintSize);
  fp1.AddMessage(msg_a);
  fp1.AddMessage(msg_b);

  // fp2 processes B then A
  MultisetFingerprint fp2(kStage1, /*is_partial=*/false, run_context, kFingerprintSize);
  fp2.AddMessage(msg_b);
  fp2.AddMessage(msg_a);

  const RunFingerprint r1 = fp1.Build();
  const RunFingerprint r2 = fp2.Build();

  ASSERT_EQ(r1.entries_size(), 1);
  ASSERT_EQ(r2.entries_size(), 1);
  EXPECT_EQ(r1.entries(0).fingerprint(), r2.entries(0).fingerprint());
  EXPECT_TRUE(fp1.VerifyComplete(r2));
  EXPECT_TRUE(fp2.VerifyComplete(r1));
}

TEST(MultisetFingerprintTest, MessageContentDifferenceCausesVerificationFailure) {
  const MetricEpochKey key = CreateMetricEpochKey(1, 2, 3);

  ShuffledMessage msg_orig;
  *msg_orig.mutable_metric_epoch_key() = key;
  msg_orig.set_is_padding(false);
  msg_orig.set_customer_message("legitimate_message");

  ShuffledMessage msg_tampered;
  *msg_tampered.mutable_metric_epoch_key() = key;
  msg_tampered.set_is_padding(false);
  msg_tampered.set_customer_message("tampered_message");

  const RunContext run_context = CreateTestRunContext("run-sec", kTestRunHashKey);
  MultisetFingerprint fp_orig(kStage1, /*is_partial=*/false, run_context, kFingerprintSize);
  fp_orig.AddMessage(msg_orig);

  MultisetFingerprint fp_tampered(kStage1, /*is_partial=*/false, run_context, kFingerprintSize);
  fp_tampered.AddMessage(msg_tampered);

  const RunFingerprint complete_orig = fp_orig.Build();
  const RunFingerprint complete_tampered = fp_tampered.Build();

  // Same count, but different payloads -> verification must fail
  EXPECT_EQ(complete_orig.entries(0).message_count(), complete_tampered.entries(0).message_count());
  EXPECT_NE(complete_orig.entries(0).fingerprint(), complete_tampered.entries(0).fingerprint());
  EXPECT_FALSE(fp_orig.VerifyComplete(complete_tampered));
  EXPECT_FALSE(fp_orig.VerifyCompleteForMetricEpoch(complete_tampered, key));
}

TEST(MultisetFingerprintTest, FingerprintTamperingFailsVerification) {
  const MetricEpochKey key = CreateMetricEpochKey(1, 2, 3);

  ShuffledMessage msg;
  *msg.mutable_metric_epoch_key() = key;
  msg.set_is_padding(false);
  msg.set_customer_message("valid_payload");

  const RunContext run_context = CreateTestRunContext("run-sec", kTestRunHashKey);
  MultisetFingerprint fp(kStage1, /*is_partial=*/false, run_context, kFingerprintSize);
  fp.AddMessage(msg);

  RunFingerprint tampered_fp = fp.Build();
  ASSERT_EQ(tampered_fp.entries_size(), 1);
  std::string* fp_bytes = tampered_fp.mutable_entries(0)->mutable_fingerprint();
  ASSERT_EQ(fp_bytes->size(), MultisetFingerprint::kFingerprintSize);
  (*fp_bytes)[0] ^= 0x01;  // flip a bit

  EXPECT_FALSE(fp.VerifyComplete(tampered_fp));
  EXPECT_FALSE(fp.VerifyCompleteForMetricEpoch(tampered_fp, key));
}

TEST(MultisetFingerprintTest, SecretKeyEnforcement) {
  const MetricEpochKey key = CreateMetricEpochKey(1, 2, 3);

  ShuffledMessage msg;
  *msg.mutable_metric_epoch_key() = key;
  msg.set_is_padding(false);
  msg.set_customer_message("identical_payload");

  const RunContext ctx1 = CreateTestRunContext("run-key", "key_A");
  const RunContext ctx2 = CreateTestRunContext("run-key", "key_B");

  MultisetFingerprint fp1(kStage1, /*is_partial=*/false, ctx1, kFingerprintSize);
  fp1.AddMessage(msg);

  MultisetFingerprint fp2(kStage1, /*is_partial=*/false, ctx2, kFingerprintSize);
  fp2.AddMessage(msg);

  const RunFingerprint r1 = fp1.Build();
  const RunFingerprint r2 = fp2.Build();

  EXPECT_NE(r1.entries(0).fingerprint(), r2.entries(0).fingerprint());
  EXPECT_FALSE(fp1.VerifyComplete(r2));
}

TEST(MultisetFingerprintTest, ConstructorInitializesFromRunContext) {
  const RunContext run_context = CreateTestRunContext("ctx-run-id-12345", "ctx-hash-key-67890");

  MultisetFingerprint fp(kStage1, /*is_partial=*/false, run_context, kFingerprintSize);

  const MetricEpochKey key = CreateMetricEpochKey(1, 2, 3);
  ShuffledMessage msg;
  *msg.mutable_metric_epoch_key() = key;
  msg.set_customer_message("data");
  fp.AddMessage(msg);

  const RunFingerprint built = fp.Build();
  EXPECT_EQ(built.run_id(), "ctx-run-id-12345");
  EXPECT_EQ(built.stage_id(), kStage1);
  EXPECT_FALSE(built.is_partial());
  ASSERT_EQ(built.entries_size(), 1);
  EXPECT_EQ(built.entries(0).message_count(), 1);
  EXPECT_EQ(built.entries(0).fingerprint().size(), MultisetFingerprint::kFingerprintSize);
  EXPECT_EQ(fp.FingerprintSize(), MultisetFingerprint::kFingerprintSize);
}

TEST(MultisetFingerprintTest, CustomFingerprintSizeConstructor) {
  const RunContext run_context = CreateTestRunContext("ctx-run-id-12345", "ctx-hash-key-67890");
  constexpr std::size_t kCustomSize = 16;

  MultisetFingerprint fp(kStage1, /*is_partial=*/false, run_context, kCustomSize);
  EXPECT_EQ(fp.FingerprintSize(), kCustomSize);

  const MetricEpochKey key = CreateMetricEpochKey(1, 2, 3);
  ShuffledMessage msg;
  *msg.mutable_metric_epoch_key() = key;
  msg.set_customer_message("data");
  fp.AddMessage(msg);

  const RunFingerprint built = fp.Build();
  ASSERT_EQ(built.entries_size(), 1);
  EXPECT_EQ(built.entries(0).fingerprint().size(), kCustomSize);
}

std::string ReadTestFile(absl::string_view relative_path) {
  std::vector<std::filesystem::path> candidate_paths;
  candidate_paths.push_back(std::string(relative_path));

  const char* test_srcdir = std::getenv("TEST_SRCDIR");
  if (test_srcdir != nullptr) {
    const char* test_workspace = std::getenv("TEST_WORKSPACE");
    const std::string workspace =
        (test_workspace != nullptr && *test_workspace != '\0') ? test_workspace : "_main";
    candidate_paths.push_back(std::filesystem::path(test_srcdir) / workspace /
                              std::string(relative_path));
  }

  for (const auto& path : candidate_paths) {
    if (std::filesystem::exists(path)) {
      std::ifstream file(path);
      if (file.is_open()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
      }
    }
  }

  ADD_FAILURE() << "Failed to find or read test file: " << relative_path;
  return "";
}

TEST(MultisetFingerprintTest, GoldenTestCaseMSetAddHash) {
  // Verifies the MSet-Add-Hash multiset fingerprinting implementation against
  // golden values generated by the Python reference script
  // (src/testing/generate_multiset_fingerprint_golden_test_vectors.py)
  // and checked into
  // src/crypto/testdata/multiset_fingerprint_golden_test_vectors.json.
  const std::string json_content =
      ReadTestFile("src/crypto/testdata/multiset_fingerprint_golden_test_vectors.json");
  ASSERT_FALSE(json_content.empty());

  MultisetFingerprintGoldenTestVectors golden;
  const absl::Status status = google::protobuf::util::JsonStringToMessage(json_content, &golden);
  ASSERT_TRUE(status.ok()) << status;

  const RunContext run_context = CreateTestRunContext("run-golden", golden.key());
  const MetricEpochKey key = CreateMetricEpochKey(1, 2, 3);

  // Step 1: Add first batch of messages to sketch 1
  MultisetFingerprint fp1(kStage1, /*is_partial=*/true, run_context, kFingerprintSize);
  for (const std::string& msg_text : golden.sketch1_messages()) {
    ShuffledMessage msg;
    *msg.mutable_metric_epoch_key() = key;
    msg.set_is_padding(false);
    msg.set_customer_message(msg_text);
    fp1.AddMessage(msg);
  }

  const RunFingerprint partial_fp1 = fp1.Build();
  ASSERT_EQ(partial_fp1.entries_size(), 1);
  EXPECT_EQ(partial_fp1.entries(0).message_count(), golden.sketch1_messages_size());
  EXPECT_EQ(absl::BytesToHexString(partial_fp1.entries(0).fingerprint()),
            golden.expected_sketch1_digest_hex());

  // Step 2: Add second batch of messages to sketch 2
  MultisetFingerprint fp2(kStage1, /*is_partial=*/true, run_context, kFingerprintSize);
  for (const std::string& msg_text : golden.sketch2_messages()) {
    ShuffledMessage msg;
    *msg.mutable_metric_epoch_key() = key;
    msg.set_is_padding(false);
    msg.set_customer_message(msg_text);
    fp2.AddMessage(msg);
  }

  const RunFingerprint partial_fp2 = fp2.Build();
  ASSERT_EQ(partial_fp2.entries_size(), 1);
  EXPECT_EQ(partial_fp2.entries(0).message_count(), golden.sketch2_messages_size());
  EXPECT_EQ(absl::BytesToHexString(partial_fp2.entries(0).fingerprint()),
            golden.expected_sketch2_digest_hex());

  // Step 3: Merge sketch 1 and sketch 2
  MultisetFingerprint canonical_fp(kStage1, /*is_partial=*/false, run_context, kFingerprintSize);
  EXPECT_THAT(canonical_fp.Merge(partial_fp1), IsOk());
  EXPECT_THAT(canonical_fp.Merge(partial_fp2), IsOk());

  const RunFingerprint canonical = canonical_fp.Build();
  ASSERT_EQ(canonical.entries_size(), 1);
  EXPECT_EQ(canonical.entries(0).message_count(),
            golden.sketch1_messages_size() + golden.sketch2_messages_size());
  EXPECT_EQ(absl::BytesToHexString(canonical.entries(0).fingerprint()),
            golden.expected_merged_digest_hex());

  // Step 4: Add all messages directly and verify equivalence
  MultisetFingerprint direct_fp(kStage1, /*is_partial=*/false, run_context, kFingerprintSize);
  for (const std::string& msg_text : golden.sketch1_messages()) {
    ShuffledMessage msg;
    *msg.mutable_metric_epoch_key() = key;
    msg.set_is_padding(false);
    msg.set_customer_message(msg_text);
    direct_fp.AddMessage(msg);
  }
  for (const std::string& msg_text : golden.sketch2_messages()) {
    ShuffledMessage msg;
    *msg.mutable_metric_epoch_key() = key;
    msg.set_is_padding(false);
    msg.set_customer_message(msg_text);
    direct_fp.AddMessage(msg);
  }

  const RunFingerprint direct_built = direct_fp.Build();
  ASSERT_EQ(direct_built.entries_size(), 1);
  EXPECT_EQ(direct_built.entries(0).message_count(),
            golden.sketch1_messages_size() + golden.sketch2_messages_size());
  EXPECT_EQ(absl::BytesToHexString(direct_built.entries(0).fingerprint()),
            golden.expected_merged_digest_hex());
  EXPECT_TRUE(direct_fp.VerifyComplete(canonical));
}

TEST(MultisetFingerprintTest, VerifyCompleteSucceedsWithZeroCountPartitionOmittedFromMerged) {
  const MetricEpochKey key1 = CreateMetricEpochKey(1, 2, 100);
  const MetricEpochKey key2 = CreateMetricEpochKey(1, 2, 101);

  ShuffledMessage msg;
  *msg.mutable_metric_epoch_key() = key1;
  msg.set_is_padding(false);
  msg.set_customer_message("payload1");

  const RunContext run_context = CreateTestRunContext("run-zero-count", kTestRunHashKey);
  MultisetFingerprint fp(kStage3, /*is_partial=*/false, run_context, kFingerprintSize);
  fp.AddMessage(msg);

  RunFingerprint complete_fp = fp.Build();
  // Add a second approved partition with message_count = 0 and empty fingerprint.
  FingerprintEntry* empty_entry = complete_fp.add_entries();
  *empty_entry->mutable_metric_epoch_key() = key2;
  empty_entry->set_message_count(0);
  empty_entry->set_fingerprint("");

  EXPECT_TRUE(fp.VerifyComplete(complete_fp));
}

TEST(MultisetFingerprintTest, VerifyCompleteSucceedsWithZeroCountPartition32ZeroBytes) {
  const MetricEpochKey key1 = CreateMetricEpochKey(1, 2, 100);
  const MetricEpochKey key2 = CreateMetricEpochKey(1, 2, 101);

  ShuffledMessage msg;
  *msg.mutable_metric_epoch_key() = key1;
  msg.set_is_padding(false);
  msg.set_customer_message("payload1");

  const RunContext run_context = CreateTestRunContext("run-zero-count", kTestRunHashKey);
  MultisetFingerprint fp(kStage3, /*is_partial=*/false, run_context, kFingerprintSize);
  fp.AddMessage(msg);

  RunFingerprint complete_fp = fp.Build();
  // Add a second approved partition with message_count = 0 and 32 zero bytes fingerprint.
  FingerprintEntry* empty_entry = complete_fp.add_entries();
  *empty_entry->mutable_metric_epoch_key() = key2;
  empty_entry->set_message_count(0);
  empty_entry->set_fingerprint(std::string(kFingerprintSize, '\0'));

  EXPECT_TRUE(fp.VerifyComplete(complete_fp));
}

TEST(MultisetFingerprintTest,
     VerifyCompleteReturnsFalseWhenMergedContainsUnapprovedZeroCountPartition) {
  const MetricEpochKey key1 = CreateMetricEpochKey(1, 2, 100);
  const MetricEpochKey unapproved_key = CreateMetricEpochKey(1, 2, 999);

  ShuffledMessage msg;
  *msg.mutable_metric_epoch_key() = key1;
  msg.set_is_padding(false);
  msg.set_customer_message("payload1");

  const RunContext run_context = CreateTestRunContext("run-unapproved-zero", kTestRunHashKey);
  MultisetFingerprint fp(kStage3, /*is_partial=*/false, run_context, kFingerprintSize);
  fp.AddMessage(msg);

  const RunFingerprint complete_fp = fp.Build();

  // Merge a partial fingerprint containing an unapproved zero-count partition.
  RunFingerprint partial_with_unapproved;
  partial_with_unapproved.set_run_id("run-unapproved-zero");
  partial_with_unapproved.set_stage_id(kStage3);
  partial_with_unapproved.set_is_partial(true);
  FingerprintEntry* unapproved_entry = partial_with_unapproved.add_entries();
  *unapproved_entry->mutable_metric_epoch_key() = unapproved_key;
  unapproved_entry->set_message_count(0);
  unapproved_entry->set_fingerprint("");

  ASSERT_THAT(fp.Merge(partial_with_unapproved), IsOk());
  EXPECT_FALSE(fp.VerifyComplete(complete_fp));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
