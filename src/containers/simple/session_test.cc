#include "src/containers/simple/session.h"

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/testing/fake_context.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::fcp::confidentialcompute::BlobMetadata;
using ::fcp::confidentialcompute::CommitRequest;
using ::fcp::confidentialcompute::CommitResponse;
using ::fcp::confidentialcompute::ConfigureRequest;
using ::fcp::confidentialcompute::ConfigureResponse;
using ::fcp::confidentialcompute::FinalizeRequest;
using ::fcp::confidentialcompute::FinalizeResponse;
using ::fcp::confidentialcompute::WriteFinishedResponse;
using ::fcp::confidentialcompute::WriteRequest;
using ::testing::IsEmpty;
using ::absl_testing::StatusIs;

TEST(SessionTest, ConfigureReturnsUnimplemented) {
  testing::FakeContext context;
  Session session;

  const absl::StatusOr<ConfigureResponse> response = session.Configure(ConfigureRequest(), context);

  EXPECT_THAT(response, StatusIs(absl::StatusCode::kUnimplemented));
}

TEST(SessionTest, ConfigureIsNoOp) {
  testing::FakeContext context;
  Session session;

  const absl::StatusOr<ConfigureResponse> _ = session.Configure(ConfigureRequest(), context);

  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.ReadResponses(), IsEmpty());
}

TEST(SessionTest, WriteReturnsUnimplemented) {
  testing::FakeContext context;
  Session session;

  const absl::StatusOr<WriteFinishedResponse> response = session.Write(WriteRequest(), "", context);

  EXPECT_THAT(response, StatusIs(absl::StatusCode::kUnimplemented));
}

TEST(SessionTest, WriteIsNoOp) {
  testing::FakeContext context;
  Session session;

  const absl::StatusOr<WriteFinishedResponse> _ = session.Write(WriteRequest(), "", context);

  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.ReadResponses(), IsEmpty());
}

TEST(SessionTest, CommitReturnsUnimplemented) {
  testing::FakeContext context;
  Session session;

  const absl::StatusOr<CommitResponse> response = session.Commit(CommitRequest(), context);

  EXPECT_THAT(response, StatusIs(absl::StatusCode::kUnimplemented));
}
TEST(SessionTest, CommitIsNoOp) {
  testing::FakeContext context;
  Session session;

  const absl::StatusOr<CommitResponse> _ = session.Commit(CommitRequest(), context);

  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.ReadResponses(), IsEmpty());
}

TEST(SessionTest, FinalizeReturnsUnimplemented) {
  testing::FakeContext context;
  Session session;

  const absl::StatusOr<FinalizeResponse> response =
      session.Finalize(FinalizeRequest(), BlobMetadata(), context);

  EXPECT_THAT(response, StatusIs(absl::StatusCode::kUnimplemented));
}

TEST(SessionTest, FinalizeIsNoOp) {
  testing::FakeContext context;
  Session session;

  const absl::StatusOr<FinalizeResponse> _ =
      session.Finalize(FinalizeRequest(), BlobMetadata(), context);

  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.ReadResponses(), IsEmpty());
}

}  // namespace
}  // namespace cobalt::trusted_shuffler