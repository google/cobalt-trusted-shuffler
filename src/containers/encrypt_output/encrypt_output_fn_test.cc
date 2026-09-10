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

#include "src/containers/encrypt_output/encrypt_output_fn.h"

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "gtest/gtest.h"
#include "src/testing/fake_context.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::testing::IsEmpty;

TEST(EncryptOutputFnTest, DoDoesNothing) {
  testing::FakeContext context;
  EncryptOutputFn fn;

  EXPECT_THAT(fn.Do(EncryptOutputFn::KV(google::protobuf::Any(), "value"), context), IsOk());

  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.Releasable(), IsEmpty());
}

TEST(EncryptOutputFnTest, FinalizeReplicaDoesNothing) {
  testing::FakeContext context;
  EncryptOutputFn fn;

  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.Releasable(), IsEmpty());
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
