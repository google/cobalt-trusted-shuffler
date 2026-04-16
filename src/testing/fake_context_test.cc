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

#include "src/testing/fake_context.h"

#include <utility>
#include <vector>

#include "containers/session.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace cobalt::trusted_shuffler::testing {
namespace {

using ::confidential_federated_compute::Session;
using ::fcp::confidentialcompute::ReadResponse;
using ::testing::Eq;
using ::testing::SizeIs;

TEST(FakeContextTest, Emit_StoresReadResponse) {
  FakeContext context;
  ReadResponse response;

  ASSERT_TRUE(context.Emit(response));
  EXPECT_THAT(context.ReadResponses(), SizeIs(1));
}

TEST(FakeContextTest, EmitUnencrypted_StoresKV) {
  FakeContext context;

  ASSERT_TRUE(context.EmitUnencrypted("test_value"));

  std::vector<Session::KV> unencrypted = context.Unencrypted();
  ASSERT_THAT(unencrypted, SizeIs(1));
  EXPECT_THAT(unencrypted[0].data, Eq("test_value"));
}

TEST(FakeContextTest, EmitEncrypted_StoresKeyIndexAndKV) {
  FakeContext context;
  const int key_index = 5;

  ASSERT_TRUE(context.EmitEncrypted(key_index, "crypto_value"));

  std::vector<std::pair<int, Session::KV>> encrypted = context.Encrypted();
  ASSERT_THAT(encrypted, SizeIs(1));
  EXPECT_THAT(encrypted[0].first, Eq(key_index));
  EXPECT_THAT(encrypted[0].second.data, Eq("crypto_value"));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler::testing
