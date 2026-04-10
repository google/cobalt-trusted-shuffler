#include "src/containers/simple/simple_transform.h"

#include <memory>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/testing/fake_context.h"
#include "src/testing/fake_keys.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::confidential_federated_compute::Session;
using ::fcp::confidentialcompute::BlobMetadata;
using ::fcp::confidentialcompute::BlobMetadata_Unencrypted;
using ::fcp::confidentialcompute::InitializeRequest;
using ::testing::IsEmpty;

class TestSimpleTransform : public SimpleTransform {
 public:
  using SimpleTransform::CreateSession;
  using SimpleTransform::GetKeyId;
  using SimpleTransform::ReadWriteConfigurationRequest;
  using SimpleTransform::SimpleTransform;
  using SimpleTransform::StreamInitializeTransform;
};

TEST(SimpleTransformTest, ConfigureReturnsOk) {
  testing::FakeContext context;
  TestSimpleTransform simple_transform(std::make_unique<testing::FakeSigningKey>(),
                                       std::make_unique<testing::FakeEncryptionKeyHandle>());

  absl::Status response =
      simple_transform.StreamInitializeTransform(google::protobuf::Any(), google::protobuf::Any());

  EXPECT_THAT(response, IsOk());

  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.ReadResponses(), IsEmpty());
}

TEST(SimpleTransformTest, CreateSessionReturnsOk) {
  testing::FakeContext context;
  TestSimpleTransform simple_transform(std::make_unique<testing::FakeSigningKey>(),
                                       std::make_unique<testing::FakeEncryptionKeyHandle>());

  absl::StatusOr<std::unique_ptr<Session>> response = simple_transform.CreateSession();

  ASSERT_THAT(response, IsOk());
  EXPECT_NE(response.value(), nullptr);

  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.ReadResponses(), IsEmpty());
}

TEST(SimpleTransformTest, ReadWriteConfigurationRequestReturnsOk) {
  testing::FakeContext context;
  TestSimpleTransform simple_transform(std::make_unique<testing::FakeSigningKey>(),
                                       std::make_unique<testing::FakeEncryptionKeyHandle>());

  auto response = simple_transform.ReadWriteConfigurationRequest(
      fcp::confidentialcompute::WriteConfigurationRequest());

  EXPECT_THAT(response, IsOk());

  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.ReadResponses(), IsEmpty());
}

TEST(SimpleTransformTest, GetKeyIdWithEmptyStringReturnsUnencryptedEmptyString) {
  testing::FakeContext context;
  TestSimpleTransform simple_transform(std::make_unique<testing::FakeSigningKey>(),
                                       std::make_unique<testing::FakeEncryptionKeyHandle>());
  BlobMetadata metadata;
  metadata.mutable_unencrypted();

  const absl::StatusOr<std::string> response = simple_transform.GetKeyId(metadata);

  ASSERT_THAT(response, IsOk());
  EXPECT_THAT(response.value(), IsEmpty());
}

TEST(SimpleTransformTest, GetKeyIdWithEmptyMetadataReturnsInvalidArgument) {
  testing::FakeContext context;
  TestSimpleTransform simple_transform(std::make_unique<testing::FakeSigningKey>(),
                                       std::make_unique<testing::FakeEncryptionKeyHandle>());
  BlobMetadata metadata;

  const absl::StatusOr<std::string> response = simple_transform.GetKeyId(metadata);

  EXPECT_THAT(response, StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
