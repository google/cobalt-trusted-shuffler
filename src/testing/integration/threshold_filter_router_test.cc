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

#include "src/testing/integration/threshold_filter_router.h"

#include <vector>

#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {
namespace {

using ::absl_testing::IsOk;
using ::testing::IsEmpty;
using ::testing::SizeIs;

TEST(ThresholdFilterRouterTest, PrependsVerificationPayloadAndPartitionMessages) {
  const EncryptedBlob verification_payload("verification_payload_bytes");
  const std::vector<EncryptedBlob> messages = {
      EncryptedBlob("msg_1"),
      EncryptedBlob("msg_2"),
  };

  const absl::StatusOr<KVCollection> result =
      RouteToThresholdFilter(verification_payload, messages);
  ASSERT_THAT(result.status(), IsOk());
  ASSERT_THAT(*result, SizeIs(3));
  EXPECT_EQ((*result)[0].data, "verification_payload_bytes");
  EXPECT_EQ((*result)[1].data, "msg_1");
  EXPECT_EQ((*result)[2].data, "msg_2");
  for (const auto& record : *result) {
    EXPECT_THAT(record.key.type_url(), IsEmpty());
  }
}

TEST(ThresholdFilterRouterTest, RouteWithLateVerificationPayloadPlacesPayloadLast) {
  const EncryptedBlob verification_payload("verification_payload_bytes");
  const std::vector<EncryptedBlob> messages = {
      EncryptedBlob("msg_1"),
      EncryptedBlob("msg_2"),
  };

  const absl::StatusOr<KVCollection> result =
      RouteToThresholdFilterWithLateVerificationPayload(verification_payload, messages);
  ASSERT_THAT(result.status(), IsOk());
  ASSERT_THAT(*result, SizeIs(3));
  EXPECT_EQ((*result)[0].data, "msg_1");
  EXPECT_EQ((*result)[1].data, "msg_2");
  EXPECT_EQ((*result)[2].data, "verification_payload_bytes");
}

}  // namespace
}  // namespace cobalt::trusted_shuffler::testing::integration
