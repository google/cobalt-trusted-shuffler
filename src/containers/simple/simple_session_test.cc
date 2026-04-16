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

#include "src/containers/simple/simple_session.h"

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/testing/fake_context.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
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
using ::testing::SizeIs;

TEST(SimpleSessionTest, ConfigureReturnsOk) {
  testing::FakeContext context;
  SimpleSession session;

  const absl::StatusOr<ConfigureResponse> response = session.Configure(ConfigureRequest(), context);

  EXPECT_THAT(response, IsOk());
}

TEST(SimpleSessionTest, ConfigureIsNoOp) {
  testing::FakeContext context;
  SimpleSession session;

  const absl::StatusOr<ConfigureResponse> _ = session.Configure(ConfigureRequest(), context);

  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.ReadResponses(), IsEmpty());
}

TEST(SimpleSessionTest, WriteReturnsOk) {
  testing::FakeContext context;
  SimpleSession session;

  const absl::StatusOr<WriteFinishedResponse> response = session.Write(WriteRequest(), "", context);

  EXPECT_THAT(response, IsOk());
}

TEST(SimpleSessionTest, WriteIsNoOp) {
  testing::FakeContext context;
  SimpleSession session;

  const absl::StatusOr<WriteFinishedResponse> _ = session.Write(WriteRequest(), "", context);

  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.ReadResponses(), IsEmpty());
}

TEST(SimpleSessionTest, CommitReturnsOk) {
  testing::FakeContext context;
  SimpleSession session;

  const absl::StatusOr<CommitResponse> response = session.Commit(CommitRequest(), context);

  EXPECT_THAT(response, IsOk());
}
TEST(SimpleSessionTest, CommitWorks) {
  testing::FakeContext context;
  SimpleSession session;

  ASSERT_THAT(session.Write(WriteRequest(), "a", context), IsOk());
  ASSERT_THAT(session.Write(WriteRequest(), "b", context), IsOk());

  const absl::StatusOr<CommitResponse> _ = session.Commit(CommitRequest(), context);

  int reencryption_key_index = 0;
  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  ASSERT_THAT(context.Encrypted(), SizeIs(2));
  EXPECT_THAT(context.Encrypted()[0].first, reencryption_key_index);
  EXPECT_THAT(context.Encrypted()[1].first, reencryption_key_index);
  EXPECT_THAT(context.Encrypted()[0].second.data, "a");
  EXPECT_THAT(context.Encrypted()[1].second.data, "b");
  EXPECT_THAT(context.ReadResponses(), IsEmpty());
}

TEST(SimpleSessionTest, FinalizeReturnsUnimplemented) {
  testing::FakeContext context;
  SimpleSession session;

  const absl::StatusOr<FinalizeResponse> response =
      session.Finalize(FinalizeRequest(), BlobMetadata(), context);

  EXPECT_THAT(response, StatusIs(absl::StatusCode::kUnimplemented));
}

TEST(SimpleSessionTest, FinalizeIsNoOp) {
  testing::FakeContext context;
  SimpleSession session;

  const absl::StatusOr<FinalizeResponse> _ =
      session.Finalize(FinalizeRequest(), BlobMetadata(), context);

  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.ReadResponses(), IsEmpty());
}

}  // namespace
}  // namespace cobalt::trusted_shuffler