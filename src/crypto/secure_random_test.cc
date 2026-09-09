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

#include "src/crypto/secure_random.h"

#include <string>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/crypto/verify_signed_run_context.h"

namespace cobalt::trusted_shuffler {
namespace {

using absl_testing::IsOk;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::SizeIs;

TEST(SecureRandomTest, GenerateCryptoRandomBytesReturnsRequestedLength) {
  for (std::size_t size : {0, 16, 32, 64}) {
    absl::StatusOr<std::string> bytes = GenerateCryptoRandomBytes(size);
    ASSERT_THAT(bytes, IsOk());
    EXPECT_THAT(*bytes, SizeIs(size));
  }
}

TEST(SecureRandomTest, GenerateCryptoRandomBytesGeneratesUniqueValues) {
  absl::StatusOr<std::string> bytes1 = GenerateCryptoRandomBytes(32);
  absl::StatusOr<std::string> bytes2 = GenerateCryptoRandomBytes(32);

  ASSERT_THAT(bytes1, IsOk());
  ASSERT_THAT(bytes2, IsOk());
  EXPECT_THAT(*bytes1, Ne(*bytes2));
}

TEST(SecureRandomTest, GenerateRunIdReturns32BytesAndIsUnique) {
  absl::StatusOr<std::string> run_id1 = GenerateRunId();
  absl::StatusOr<std::string> run_id2 = GenerateRunId();

  ASSERT_THAT(run_id1, IsOk());
  ASSERT_THAT(run_id2, IsOk());
  EXPECT_THAT(*run_id1, SizeIs(kRunIdByteLength));
  EXPECT_THAT(*run_id2, SizeIs(kRunIdByteLength));
  EXPECT_THAT(*run_id1, Ne(*run_id2));
}

TEST(SecureRandomTest, GenerateRunHashKeyReturns32BytesAndIsUnique) {
  absl::StatusOr<std::string> hash_key1 = GenerateRunHashKey();
  absl::StatusOr<std::string> hash_key2 = GenerateRunHashKey();

  ASSERT_THAT(hash_key1, IsOk());
  ASSERT_THAT(hash_key2, IsOk());
  EXPECT_THAT(*hash_key1, SizeIs(kRunHashKeyByteLength));
  EXPECT_THAT(*hash_key2, SizeIs(kRunHashKeyByteLength));
  EXPECT_THAT(*hash_key1, Ne(*hash_key2));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
