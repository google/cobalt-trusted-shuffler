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

#include "src/containers/run_context_initializer/run_context_initializer_fn.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "cc/crypto/signing_key.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/fake_context.h"

namespace cobalt::trusted_shuffler {
namespace {

using absl_testing::IsOk;
using absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::Ne;
using ::testing::SizeIs;

inline constexpr int kReencryptionKeyIndex = 0;

class TestSigningKey : public oak::crypto::SigningKeyHandle {
 public:
  absl::StatusOr<oak::crypto::v1::Signature> Sign(
      absl::string_view message) override {
    oak::crypto::v1::Signature sig;
    sig.set_signature("test_signature_bytes");
    return sig;
  }
};

class FailingSigningKey : public oak::crypto::SigningKeyHandle {
 public:
  absl::StatusOr<oak::crypto::v1::Signature> Sign(
      absl::string_view message) override {
    return absl::InternalError("Signing failed");
  }
};

RunConfig CreateValidRunConfig() {
  RunConfig config;
  config.set_min_epoch_index(10);
  config.set_max_epoch_index(20);
  config.set_target_batch_size(100);
  RunConfig::MetricIdentifier* metric = config.add_metric_identifiers();
  metric->set_project_id(101);
  metric->set_metric_id(202);
  return config;
}

TEST(RunContextInitializerFnTest, DoFailsOnInvalidInput) {
  TestSigningKey key_handle;
  RunContextInitializerFn fn(&key_handle);
  testing::FakeContext context;

  RunContextInitializerFn::KV input;
  input.data = "invalid_run_config_bytes";

  EXPECT_THAT(fn.Do(std::move(input), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RunContextInitializerFnTest, DoFailsOnEmptyMetricIdentifiers) {
  TestSigningKey key_handle;
  RunContextInitializerFn fn(&key_handle);
  testing::FakeContext context;

  RunConfig config = CreateValidRunConfig();
  config.clear_metric_identifiers();

  RunContextInitializerFn::KV input;
  input.data = config.SerializeAsString();

  EXPECT_THAT(fn.Do(std::move(input), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RunContextInitializerFnTest, DoFailsOnZeroProjectId) {
  TestSigningKey key_handle;
  RunContextInitializerFn fn(&key_handle);
  testing::FakeContext context;

  RunConfig config = CreateValidRunConfig();
  config.mutable_metric_identifiers(0)->set_project_id(0);

  RunContextInitializerFn::KV input;
  input.data = config.SerializeAsString();

  EXPECT_THAT(fn.Do(std::move(input), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RunContextInitializerFnTest, DoFailsOnNegativeProjectId) {
  TestSigningKey key_handle;
  RunContextInitializerFn fn(&key_handle);
  testing::FakeContext context;

  RunConfig config = CreateValidRunConfig();
  config.mutable_metric_identifiers(0)->set_project_id(-1);

  RunContextInitializerFn::KV input;
  input.data = config.SerializeAsString();

  EXPECT_THAT(fn.Do(std::move(input), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RunContextInitializerFnTest, DoFailsOnZeroMetricId) {
  TestSigningKey key_handle;
  RunContextInitializerFn fn(&key_handle);
  testing::FakeContext context;

  RunConfig config = CreateValidRunConfig();
  config.mutable_metric_identifiers(0)->set_project_id(101);
  config.mutable_metric_identifiers(0)->set_metric_id(0);

  RunContextInitializerFn::KV input;
  input.data = config.SerializeAsString();

  EXPECT_THAT(fn.Do(std::move(input), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RunContextInitializerFnTest, DoFailsOnNegativeMetricId) {
  TestSigningKey key_handle;
  RunContextInitializerFn fn(&key_handle);
  testing::FakeContext context;

  RunConfig config = CreateValidRunConfig();
  config.mutable_metric_identifiers(0)->set_project_id(101);
  config.mutable_metric_identifiers(0)->set_metric_id(-1);

  RunContextInitializerFn::KV input;
  input.data = config.SerializeAsString();

  EXPECT_THAT(fn.Do(std::move(input), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RunContextInitializerFnTest, DoFailsOnInvalidEpochRange) {
  TestSigningKey key_handle;
  RunContextInitializerFn fn(&key_handle);
  testing::FakeContext context;

  RunConfig config = CreateValidRunConfig();
  config.set_min_epoch_index(20);
  config.set_max_epoch_index(10);  // max < min

  RunContextInitializerFn::KV input;
  input.data = config.SerializeAsString();

  EXPECT_THAT(fn.Do(std::move(input), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RunContextInitializerFnTest, DoSucceedsWhenMinEqualsMaxEpochIndex) {
  TestSigningKey key_handle;
  RunContextInitializerFn fn(&key_handle);
  testing::FakeContext context;

  RunConfig config = CreateValidRunConfig();
  config.set_min_epoch_index(10);
  config.set_max_epoch_index(10);  // max == min is valid

  RunContextInitializerFn::KV input;
  input.data = config.SerializeAsString();

  EXPECT_THAT(fn.Do(std::move(input), context), IsOk());
}

TEST(RunContextInitializerFnTest, DoFailsOnZeroTargetBatchSize) {
  TestSigningKey key_handle;
  RunContextInitializerFn fn(&key_handle);
  testing::FakeContext context;

  RunConfig config = CreateValidRunConfig();
  config.set_target_batch_size(0);

  RunContextInitializerFn::KV input;
  input.data = config.SerializeAsString();

  EXPECT_THAT(fn.Do(std::move(input), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RunContextInitializerFnTest, DoFailsOnNegativeTargetBatchSize) {
  TestSigningKey key_handle;
  RunContextInitializerFn fn(&key_handle);
  testing::FakeContext context;

  RunConfig config = CreateValidRunConfig();
  config.set_target_batch_size(-5);

  RunContextInitializerFn::KV input;
  input.data = config.SerializeAsString();

  EXPECT_THAT(fn.Do(std::move(input), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(RunContextInitializerFnTest, DoFailsOnNullSigningKey) {
  RunContextInitializerFn fn(nullptr);
  testing::FakeContext context;

  RunConfig config = CreateValidRunConfig();

  RunContextInitializerFn::KV input;
  input.data = config.SerializeAsString();

  EXPECT_THAT(fn.Do(std::move(input), context),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(RunContextInitializerFnTest, DoFailsOnSigningError) {
  FailingSigningKey key_handle;
  RunContextInitializerFn fn(&key_handle);
  testing::FakeContext context;

  RunConfig config = CreateValidRunConfig();

  RunContextInitializerFn::KV input;
  input.data = config.SerializeAsString();

  EXPECT_THAT(fn.Do(std::move(input), context),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(RunContextInitializerFnTest, DoEmitsSignedRunContextWithKeyIndex1) {
  TestSigningKey key_handle;
  RunContextInitializerFn fn(&key_handle);
  testing::FakeContext context;

  RunConfig config;
  config.set_min_epoch_index(10);
  config.set_max_epoch_index(20);
  config.set_target_batch_size(100);
  RunConfig::MetricIdentifier* metric = config.add_metric_identifiers();
  metric->set_project_id(101);
  metric->set_metric_id(202);

  RunContextInitializerFn::KV input;
  input.data = config.SerializeAsString();

  ASSERT_THAT(fn.Do(std::move(input), context), IsOk());

  std::vector<std::pair<int, RunContextInitializerFn::KV>> encrypted_items =
      context.Encrypted();
  ASSERT_THAT(encrypted_items, SizeIs(1));

  const auto& [key_index, output_kv] = encrypted_items[0];
  EXPECT_THAT(key_index, Eq(kReencryptionKeyIndex));

  SignedRunContext signed_run_context;
  ASSERT_TRUE(signed_run_context.ParseFromString(output_kv.data));
  EXPECT_THAT(signed_run_context.signature(), Eq("test_signature_bytes"));

  RunContext run_context;
  ASSERT_TRUE(
      run_context.ParseFromString(signed_run_context.serialized_run_context()));
  EXPECT_THAT(run_context.run_id(), SizeIs(32));
  EXPECT_THAT(run_context.run_hash_key(), SizeIs(32));
  EXPECT_THAT(run_context.run_id(), Ne(run_context.run_hash_key()));
  EXPECT_THAT(run_context.run_config().min_epoch_index(), Eq(10));
  EXPECT_THAT(run_context.run_config().max_epoch_index(), Eq(20));
  EXPECT_THAT(run_context.run_config().target_batch_size(), Eq(100));
  ASSERT_THAT(run_context.run_config().metric_identifiers(), SizeIs(1));
  EXPECT_THAT(run_context.run_config().metric_identifiers(0).project_id(),
              Eq(101));
  EXPECT_THAT(run_context.run_config().metric_identifiers(0).metric_id(),
              Eq(202));
}

TEST(RunContextInitializerFnTest, GeneratesUniqueCryptoRandomKeysPerCall) {
  TestSigningKey key_handle;
  RunContextInitializerFn fn(&key_handle);
  testing::FakeContext context1;
  testing::FakeContext context2;

  RunConfig config = CreateValidRunConfig();

  RunContextInitializerFn::KV input1;
  input1.data = config.SerializeAsString();
  RunContextInitializerFn::KV input2;
  input2.data = config.SerializeAsString();

  ASSERT_THAT(fn.Do(std::move(input1), context1), IsOk());
  ASSERT_THAT(fn.Do(std::move(input2), context2), IsOk());

  std::vector<std::pair<int, RunContextInitializerFn::KV>> encrypted_items1 =
      context1.Encrypted();
  std::vector<std::pair<int, RunContextInitializerFn::KV>> encrypted_items2 =
      context2.Encrypted();
  ASSERT_THAT(encrypted_items1, SizeIs(1));
  ASSERT_THAT(encrypted_items2, SizeIs(1));

  SignedRunContext signed1, signed2;
  ASSERT_TRUE(signed1.ParseFromString(encrypted_items1[0].second.data));
  ASSERT_TRUE(signed2.ParseFromString(encrypted_items2[0].second.data));

  RunContext rc1, rc2;
  ASSERT_TRUE(rc1.ParseFromString(signed1.serialized_run_context()));
  ASSERT_TRUE(rc2.ParseFromString(signed2.serialized_run_context()));

  EXPECT_THAT(rc1.run_id(), Ne(rc2.run_id()));
  EXPECT_THAT(rc1.run_hash_key(), Ne(rc2.run_hash_key()));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
