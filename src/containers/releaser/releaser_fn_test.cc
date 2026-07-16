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

#include "src/containers/releaser/releaser_fn.h"

#include <vector>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/testing/fake_context.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::cobalt::trusted_shuffler::testing::FakeContext;
using ::fcp::confidentialcompute::CommitRequest;
using ::fcp::confidentialcompute::ConfigureRequest;
using ::testing::Eq;
using ::testing::SizeIs;

TEST(ReleaserFnTest, ConfigureReturnsOk) {
  FakeContext context;
  ReleaserFn fn;

  EXPECT_THAT(fn.Configure(ConfigureRequest(), context), IsOk());
}

TEST(ReleaserFnTest, DoEmitsUnencrypted) {
  FakeContext context;
  ReleaserFn fn;
  const std::string message = "hello world";

  EXPECT_THAT(fn.Do(ReleaserFn::KV(google::protobuf::Any(), message), context),
              IsOk());

  const std::vector<ReleaserFn::KV> unencrypted_list = context.Unencrypted();
  ASSERT_THAT(unencrypted_list, SizeIs(1));

  EXPECT_THAT(unencrypted_list[0].data, Eq(message));
}

TEST(ReleaserFnTest, CommitReturnsOk) {
  FakeContext context;
  ReleaserFn fn;

  EXPECT_THAT(fn.Commit(CommitRequest(), context), IsOk());
}

TEST(ReleaserFnTest, FinalizeReplicaReturnsOk) {
  FakeContext context;
  ReleaserFn fn;

  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
