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

#include "src/containers/releaser/releaser_transform.h"

#include <memory>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/testing/fake_keys.h"
#include "src/testing/testable_transform.h"

namespace cobalt::trusted_shuffler {
namespace {

using absl_testing::IsOk;
using absl_testing::StatusIs;
using confidential_federated_compute::Session;
using fcp::confidentialcompute::BlobMetadata;
using fcp::confidentialcompute::BlobMetadata_Unencrypted;
using fcp::confidentialcompute::InitializeRequest;
using ::testing::IsEmpty;

using TestReleaserTransform =
    cobalt::trusted_shuffler::testing::TestableTransform<ReleaserTransform>;

TEST(ReleaserTransformTest, ConfigureReturnsOk) {
  TestReleaserTransform releaser_transform(
      std::make_unique<testing::FakeSigningKey>(),
      std::make_unique<testing::FakeEncryptionKeyHandle>());

  const absl::Status response = releaser_transform.StreamInitializeTransform(
      google::protobuf::Any(), google::protobuf::Any());

  EXPECT_THAT(response, IsOk());
}

TEST(ReleaserTransformTest, CreateSessionReturnsOk) {
  TestReleaserTransform releaser_transform(
      std::make_unique<testing::FakeSigningKey>(),
      std::make_unique<testing::FakeEncryptionKeyHandle>());

  const absl::StatusOr<std::unique_ptr<Session>> response = releaser_transform.CreateSession();

  ASSERT_THAT(response, IsOk());
  EXPECT_NE(response.value(), nullptr);
}

TEST(ReleaserTransformTest, ReadWriteConfigurationRequestReturnsOk) {
  TestReleaserTransform releaser_transform(
      std::make_unique<testing::FakeSigningKey>(),
      std::make_unique<testing::FakeEncryptionKeyHandle>());

  const absl::Status response = releaser_transform.ReadWriteConfigurationRequest(
      fcp::confidentialcompute::WriteConfigurationRequest());

  EXPECT_THAT(response, IsOk());
}

TEST(ReleaserTransformTest,
     GetKeyIdWithEmptyStringReturnsUnencryptedEmptyString) {
  TestReleaserTransform releaser_transform(
      std::make_unique<testing::FakeSigningKey>(),
      std::make_unique<testing::FakeEncryptionKeyHandle>());
  BlobMetadata metadata;
  metadata.mutable_unencrypted();

  const absl::StatusOr<std::string> response =
      releaser_transform.GetKeyId(metadata);

  ASSERT_THAT(response, IsOk());
  EXPECT_THAT(response.value(), IsEmpty());
}

TEST(ReleaserTransformTest, GetKeyIdWithEmptyMetadataReturnsInvalidArgument) {
  TestReleaserTransform releaser_transform(
      std::make_unique<testing::FakeSigningKey>(),
      std::make_unique<testing::FakeEncryptionKeyHandle>());
  BlobMetadata metadata;

  const absl::StatusOr<std::string> response =
      releaser_transform.GetKeyId(metadata);

  EXPECT_THAT(response, StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
