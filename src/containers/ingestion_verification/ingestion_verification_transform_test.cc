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

#include "src/containers/ingestion_verification/ingestion_verification_transform.h"

#include <memory>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/containers/ingestion_verification/ingestion_verification_fn.h"
#include "src/testing/fake_keys.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;

TEST(IngestionVerificationTransformTest, CreateIngestionVerificationTransformReturnsNonNull) {
  std::unique_ptr<confidential_federated_compute::fns::FnConfidentialTransform> transform =
      CreateIngestionVerificationTransform(std::make_unique<testing::FakeSigningKey>(),
                                           std::make_unique<testing::FakeEncryptionKeyHandle>());

  EXPECT_NE(transform, nullptr);
}

TEST(IngestionVerificationTransformTest,
     IngestionVerificationFnFactoryReturnsIngestionVerificationFn) {
  IngestionVerificationFnFactory factory;
  absl::StatusOr<std::unique_ptr<confidential_federated_compute::fns::Fn>> fn = factory.CreateFn();
  ASSERT_THAT(fn, IsOk());

  IngestionVerificationFn* iv_fn = dynamic_cast<IngestionVerificationFn*>(fn.value().get());
  ASSERT_NE(iv_fn, nullptr);
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
