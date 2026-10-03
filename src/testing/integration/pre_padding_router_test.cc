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

#include "src/testing/integration/pre_padding_router.h"

#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {
namespace {

using ::testing::IsEmpty;
using ::testing::SizeIs;

TEST(PrePaddingRouterTest, RoutesIngestionVerificationPayload) {
  EncryptedBlob payload(Node::kVerifiedIngestion, "test-ciphertext");

  absl::StatusOr<KVCollection> inputs = RouteToPrePadding(payload);

  ASSERT_TRUE(inputs.ok());
  EXPECT_THAT(*inputs, SizeIs(1));

  EXPECT_TRUE((*inputs)[0].key.type_url().empty());
  EXPECT_EQ((*inputs)[0].data, "test-ciphertext");
}

}  // namespace
}  // namespace cobalt::trusted_shuffler::testing::integration
