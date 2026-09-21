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

#include "src/testing/integration/encrypt_output_router.h"

#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "gtest/gtest.h"
#include "src/proto/common.pb.h"
#include "src/proto/release.pb.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::IsEmpty;
using ::testing::SizeIs;

TEST(EncryptOutputRouterTest, PrependsVerificationPayloadBeforeGatheredMessages) {
  const EncryptedBlob verification_payload("verification_payload_bytes");
  const std::vector<EncryptedBlob> messages = {
      EncryptedBlob("msg_0"),
      EncryptedBlob("msg_1"),
  };

  const absl::StatusOr<std::vector<KVCollection>> shards =
      RouteToEncryptOutput(verification_payload, messages);
  ASSERT_THAT(shards, IsOk());
  ASSERT_THAT(*shards, SizeIs(1));
  ASSERT_THAT((*shards)[0], SizeIs(3));
  EXPECT_EQ((*shards)[0][0].data, "verification_payload_bytes");
  EXPECT_EQ((*shards)[0][1].data, "msg_0");
  EXPECT_EQ((*shards)[0][2].data, "msg_1");
}

TEST(EncryptOutputRouterTest, DistributesMessagesAcrossTwoShardsWithVerificationPayloadFirst) {
  const EncryptedBlob verification_payload("verification_payload_bytes");
  const std::vector<EncryptedBlob> messages = {
      EncryptedBlob("msg_0"),
      EncryptedBlob("msg_1"),
      EncryptedBlob("msg_2"),
  };

  const absl::StatusOr<std::vector<KVCollection>> shards =
      RouteToTwoShardEncryptOutput(verification_payload, messages);
  ASSERT_THAT(shards, IsOk());
  ASSERT_THAT(*shards, SizeIs(2));
  ASSERT_THAT((*shards)[0], SizeIs(3));
  EXPECT_EQ((*shards)[0][0].data, "verification_payload_bytes");
  EXPECT_EQ((*shards)[0][1].data, "msg_0");
  EXPECT_EQ((*shards)[0][2].data, "msg_2");
  ASSERT_THAT((*shards)[1], SizeIs(2));
  EXPECT_EQ((*shards)[1][0].data, "verification_payload_bytes");
  EXPECT_EQ((*shards)[1][1].data, "msg_1");
}

TEST(EncryptOutputRouterTest, EmptyInputRouterReturnsSingleEmptyShard) {
  const EncryptedBlob verification_payload("verification_payload_bytes");
  const std::vector<EncryptedBlob> messages = {EncryptedBlob("msg_0")};

  const absl::StatusOr<std::vector<KVCollection>> shards =
      RouteToEncryptOutputWithEmptyInput(verification_payload, messages);
  ASSERT_THAT(shards, IsOk());
  ASSERT_THAT(*shards, SizeIs(1));
  EXPECT_THAT((*shards)[0], IsEmpty());
}

TEST(EncryptOutputRouterTest, LateVerificationPayloadRouterAppendsPayloadAfterMessages) {
  const EncryptedBlob verification_payload("verification_payload_bytes");
  const std::vector<EncryptedBlob> messages = {
      EncryptedBlob("msg_0"),
      EncryptedBlob("msg_1"),
  };

  const absl::StatusOr<std::vector<KVCollection>> shards =
      RouteToEncryptOutputWithLateVerificationPayload(verification_payload, messages);
  ASSERT_THAT(shards, IsOk());
  ASSERT_THAT(*shards, SizeIs(1));
  ASSERT_THAT((*shards)[0], SizeIs(3));
  EXPECT_EQ((*shards)[0][0].data, "msg_0");
  EXPECT_EQ((*shards)[0][1].data, "msg_1");
  EXPECT_EQ((*shards)[0][2].data, "verification_payload_bytes");
}

TEST(EncryptOutputRouterTest, PartiallyShuffledMessageRouterInjectsShuffleCountOneMessage) {
  const EncryptedBlob verification_payload("verification_payload_bytes");
  const std::vector<EncryptedBlob> messages = {EncryptedBlob("msg_0")};

  const absl::StatusOr<std::vector<KVCollection>> shards =
      RoutePartiallyShuffledMessageToEncryptOutput(verification_payload, messages);
  ASSERT_THAT(shards, IsOk());
  ASSERT_THAT(*shards, SizeIs(1));
  ASSERT_THAT((*shards)[0], SizeIs(3));
  EXPECT_EQ((*shards)[0][0].data, "verification_payload_bytes");
  ShuffledMessage injected;
  ASSERT_TRUE(injected.ParseFromString((*shards)[0][1].data));
  EXPECT_EQ(injected.shuffle_count(), 1);
  EXPECT_EQ((*shards)[0][2].data, "msg_0");
}

TEST(EncryptOutputRouterTest, SplitsMessageOutputsAndPartialReportsByReleaseOutputHeader) {
  ReleaseOutputHeader msg_header;
  msg_header.set_type(ReleaseOutputHeader::TYPE_MESSAGE);
  google::protobuf::Any msg_key;
  msg_key.PackFrom(msg_header);

  ReleaseOutputHeader report_header;
  report_header.set_type(ReleaseOutputHeader::TYPE_REPORT);
  google::protobuf::Any report_key;
  report_key.PackFrom(report_header);

  KVBlobStageOutputs stage_outputs;
  stage_outputs.unencrypted.emplace_back(msg_key, "encrypted_message_output_bytes");
  stage_outputs.encrypted.emplace_back(report_key, "partial_release_report_bytes");

  const absl::StatusOr<EncryptOutputOutputs> split = SplitEncryptOutputOutputs(stage_outputs);
  ASSERT_THAT(split, IsOk());
  ASSERT_THAT(split->encrypted_message_outputs, SizeIs(1));
  EXPECT_EQ(split->encrypted_message_outputs[0].plaintext(), "encrypted_message_output_bytes");
  ASSERT_THAT(split->partial_release_reports, SizeIs(1));
  EXPECT_EQ(split->partial_release_reports[0].ciphertext(), "partial_release_report_bytes");
}

TEST(EncryptOutputRouterTest, FailsWithFailedPreconditionWhenIntegrityFaultEmitted) {
  ReleaseOutputHeader fault_header;
  fault_header.set_type(ReleaseOutputHeader::TYPE_INTEGRITY_FAULT);
  google::protobuf::Any fault_key;
  fault_key.PackFrom(fault_header);

  KVBlobStageOutputs stage_outputs;
  stage_outputs.unencrypted.emplace_back(fault_key, IntegrityFault().SerializeAsString());

  EXPECT_THAT(SplitEncryptOutputOutputs(stage_outputs),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(EncryptOutputRouterTest, FailsWithInternalErrorOnUnexpectedEncryptedHeaderType) {
  ReleaseOutputHeader msg_header;
  msg_header.set_type(ReleaseOutputHeader::TYPE_MESSAGE);
  google::protobuf::Any msg_key;
  msg_key.PackFrom(msg_header);

  KVBlobStageOutputs stage_outputs;
  stage_outputs.encrypted.emplace_back(msg_key, "unexpected_encrypted_record");

  EXPECT_THAT(SplitEncryptOutputOutputs(stage_outputs), StatusIs(absl::StatusCode::kInternal));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler::testing::integration
