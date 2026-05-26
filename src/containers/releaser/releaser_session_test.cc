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

#include "src/containers/releaser/releaser_session.h"

#include <vector>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/testing/fake_context.h"

namespace cobalt::trusted_shuffler {
namespace {

static constexpr int kKeyId = 1;

using absl_testing::IsOk;
using absl_testing::StatusIs;
using fcp::confidentialcompute::BlobMetadata;
using fcp::confidentialcompute::CommitRequest;
using fcp::confidentialcompute::CommitResponse;
using fcp::confidentialcompute::ConfigureRequest;
using fcp::confidentialcompute::ConfigureResponse;
using fcp::confidentialcompute::FinalizeRequest;
using fcp::confidentialcompute::FinalizeResponse;
using fcp::confidentialcompute::WriteFinishedResponse;
using fcp::confidentialcompute::WriteRequest;
using ::testing::Eq;
using testing::FakeContext;
using ::testing::IsEmpty;
using ::testing::SizeIs;

TEST(ReleaserSessionTest, ConfigureReturnsOk) {
  FakeContext context;
  ReleaserSession session(kKeyId);

  EXPECT_THAT(session.Configure(ConfigureRequest(), context), IsOk());
}

TEST(ReleaserSessionTest, WriteEmitsReleasable) {
  FakeContext context;
  ReleaserSession session(kKeyId);
  const std::string message = "hello world";

  EXPECT_THAT(session.Write(WriteRequest(), message, context), IsOk());

  const std::vector<FakeContext::ReleasableInfo> releasable_list = context.Releasable();
  ASSERT_THAT(releasable_list, SizeIs(1));

  const FakeContext::ReleasableInfo& releasable = releasable_list[0];
  EXPECT_THAT(releasable.reencryption_key_index, Eq(kKeyId));
  EXPECT_THAT(releasable.kv.data, Eq(message));
  EXPECT_THAT(releasable.src_state, Eq(std::nullopt));
  EXPECT_THAT(releasable.dst_state, Eq("stopped"));
}

TEST(ReleaserSessionTest, CommitReturnsOk) {
  FakeContext context;
  ReleaserSession session(kKeyId);

  EXPECT_THAT(session.Commit(CommitRequest(), context), IsOk());
}

TEST(ReleaserSessionTest, FinalizeReturnsReleaseToken) {
  FakeContext context;
  ReleaserSession session(kKeyId);

  // Write a message to trigger EmitReleasable which sets the release token
  ASSERT_THAT(session.Write(WriteRequest(), "message", context), IsOk());

  const absl::StatusOr<FinalizeResponse> response =
      session.Finalize(FinalizeRequest(), BlobMetadata(), context);
  ASSERT_THAT(response, IsOk());
  EXPECT_THAT(response->release_token(), Eq("fake_release_token"));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler