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

#include "src/containers/encrypt_output/encrypt_output_transform.h"

#include <memory>

#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/containers/encrypt_output/encrypt_output_fn.h"
#include "src/testing/fake_keys.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::confidential_federated_compute::fns::Fn;
using ::confidential_federated_compute::fns::FnConfidentialTransform;

TEST(EncryptOutputTransformTest, CreateEncryptOutputTransformReturnsNonNull) {
  const std::unique_ptr<FnConfidentialTransform> transform =
      CreateEncryptOutputTransform(std::make_unique<testing::FakeSigningKey>(),
                                   std::make_unique<testing::FakeEncryptionKeyHandle>());

  EXPECT_NE(transform, nullptr);
}

TEST(EncryptOutputTransformTest, EncryptOutputFnFactoryReturnsEncryptOutputFn) {
  EncryptOutputFnFactory factory;
  const absl::StatusOr<std::unique_ptr<Fn>> fn = factory.CreateFn();
  ASSERT_THAT(fn, IsOk());

  const EncryptOutputFn* const encrypt_output_fn = dynamic_cast<EncryptOutputFn*>(fn.value().get());
  ASSERT_NE(encrypt_output_fn, nullptr);
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
