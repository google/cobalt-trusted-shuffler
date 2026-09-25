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

#include "src/testing/integration/key_releaser_router.h"

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/crypto/release_encrypter.h"
#include "src/proto/common.pb.h"
#include "src/proto/release.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/integration/records.h"
#include "src/testing/test_utils.h"

namespace cobalt::trusted_shuffler::testing::integration {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::ElementsAre;
using ::testing::SizeIs;

TEST(KeyReleaserRouterTest, PrependsVerificationPayloadBeforePartialReports) {
  const EncryptedBlob verification_payload("verification_payload_bytes");
  const std::vector<EncryptedBlob> reports = {
      EncryptedBlob("report_0"),
      EncryptedBlob("report_1"),
  };

  const absl::StatusOr<KVCollection> inputs = RouteToKeyReleaser(verification_payload, reports);
  ASSERT_THAT(inputs, IsOk());
  ASSERT_THAT(*inputs, SizeIs(3));
  EXPECT_EQ((*inputs)[0].data, "verification_payload_bytes");
  EXPECT_EQ((*inputs)[1].data, "report_0");
  EXPECT_EQ((*inputs)[2].data, "report_1");
}

TEST(KeyReleaserRouterTest, DecryptsAndRoutesCustomerMessagesByMetricEpochKey) {
  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());

  RunContext run_context;
  run_context.set_release_key(*release_key);
  const absl::StatusOr<ReleaseEncrypter> encrypter = ReleaseEncrypter::Create(run_context);
  ASSERT_THAT(encrypter, IsOk());

  const MetricEpochKey partition =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 2, .epoch_index = 5});
  const absl::StatusOr<std::string> ct =
      encrypter->EncryptForRelease("decrypted_customer_payload", /*associated_data=*/"");
  ASSERT_THAT(ct, IsOk());

  EncryptedMessageOutput msg_output;
  *msg_output.mutable_metric_epoch_key() = partition;
  msg_output.set_encrypted_message(*ct);

  DecryptionKeyRelease release_msg;
  release_msg.mutable_released_key_report()->set_release_key(*release_key);

  BlobStageOutputs key_releaser_output;
  key_releaser_output.unencrypted.emplace_back(release_msg.SerializeAsString());

  const std::vector<UnencryptedBlob> encrypted_outputs = {
      UnencryptedBlob(msg_output.SerializeAsString()),
  };

  const absl::StatusOr<absl::flat_hash_map<MetricEpochKey, std::vector<std::string>>> routed =
      DecryptAndRouteReleasedMessages(key_releaser_output, encrypted_outputs);
  ASSERT_THAT(routed, IsOk());
  ASSERT_TRUE(routed->contains(partition));
  EXPECT_THAT(routed->at(partition), ElementsAre("decrypted_customer_payload"));
}

TEST(KeyReleaserRouterTest, FailsWhenDecryptionKeyReleaseContainsIntegrityFault) {
  DecryptionKeyRelease release_msg;
  release_msg.mutable_integrity_fault();

  BlobStageOutputs key_releaser_output;
  key_releaser_output.unencrypted.emplace_back(release_msg.SerializeAsString());

  EXPECT_THAT(
      DecryptAndRouteReleasedMessages(key_releaser_output, /*encrypted_message_outputs=*/{}),
      StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(KeyReleaserRouterTest, FailsWhenKeyReleaserEmitsNoOutput) {
  BlobStageOutputs key_releaser_output;

  EXPECT_THAT(
      DecryptAndRouteReleasedMessages(key_releaser_output, /*encrypted_message_outputs=*/{}),
      StatusIs(absl::StatusCode::kFailedPrecondition));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler::testing::integration
