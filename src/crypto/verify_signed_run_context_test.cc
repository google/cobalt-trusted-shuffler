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

#include "src/crypto/verify_signed_run_context.h"

#include <string>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/crypto/secure_random.h"
#include "src/proto/run_context.pb.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::Eq;

constexpr absl::string_view kValidSignature = "valid_test_signature";

RunConfig CreateValidRunConfig() {
  RunConfig config;
  config.set_min_epoch_index(10);
  config.set_max_epoch_index(20);
  config.set_target_batch_size(100);

  RunConfig::MetricIdentifier* const metric = config.add_metric_identifiers();
  metric->set_project_id(101);
  metric->set_metric_id(202);

  return config;
}

RunContext CreateValidRunContext() {
  RunContext context;
  context.set_run_id(std::string(32, 'a'));
  context.set_run_hash_key(std::string(32, 'b'));
  *context.mutable_run_config() = CreateValidRunConfig();
  return context;
}

SignedRunContext CreateValidSignedRunContext() {
  const RunContext run_context = CreateValidRunContext();
  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));
  return signed_context;
}

TEST(VerifySignedRunContextTest, ValidSignedRunContextSucceeds) {
  const SignedRunContext signed_context = CreateValidSignedRunContext();

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  ASSERT_THAT(validated_context, IsOk());

  EXPECT_THAT(validated_context->run_id(), Eq(std::string(32, 'a')));
  EXPECT_THAT(validated_context->run_hash_key(), Eq(std::string(32, 'b')));
  EXPECT_THAT(validated_context->run_config().target_batch_size(), Eq(100));
  EXPECT_THAT(validated_context->run_config().metric_identifiers_size(), Eq(1));
}

TEST(VerifySignedRunContextTest, EmptySignatureReturnsInvalidArgument) {
  SignedRunContext signed_context = CreateValidSignedRunContext();
  signed_context.clear_signature();

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, EmptySerializedRunContextReturnsInvalidArgument) {
  SignedRunContext signed_context;
  signed_context.set_signature(std::string(kValidSignature));
  signed_context.clear_serialized_run_context();

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, CorruptedSerializedRunContextReturnsInvalidArgument) {
  SignedRunContext signed_context;
  signed_context.set_signature(std::string(kValidSignature));
  signed_context.set_serialized_run_context("invalid_not_a_proto_stream");

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, EmptyRunIdReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.clear_run_id();

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, ShortRunIdReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.set_run_id(std::string(16, 'x'));

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, LongRunIdReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.set_run_id(std::string(33, 'x'));

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, EmptyRunHashKeyReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.clear_run_hash_key();

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, ShortRunHashKeyReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.set_run_hash_key(std::string(31, 'y'));

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, LongRunHashKeyReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.set_run_hash_key(std::string(64, 'y'));

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, MissingRunConfigReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.clear_run_config();

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, EmptyMetricIdentifiersReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->clear_metric_identifiers();

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, ZeroProjectIdReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->mutable_metric_identifiers(0)->set_project_id(0);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, NegativeProjectIdReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->mutable_metric_identifiers(0)->set_project_id(-10);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, ZeroMetricIdReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->mutable_metric_identifiers(0)->set_metric_id(0);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, NegativeMetricIdReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->mutable_metric_identifiers(0)->set_metric_id(-1);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, DuplicateMetricIdentifiersReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  RunConfig::MetricIdentifier* const duplicate_metric =
      run_context.mutable_run_config()->add_metric_identifiers();
  duplicate_metric->set_project_id(101);
  duplicate_metric->set_metric_id(202);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, MultipleDistinctMetricIdentifiersSucceeds) {
  RunContext run_context = CreateValidRunContext();
  RunConfig* const run_config = run_context.mutable_run_config();

  RunConfig::MetricIdentifier* const second_metric = run_config->add_metric_identifiers();
  second_metric->set_project_id(101);
  second_metric->set_metric_id(303);

  RunConfig::MetricIdentifier* const third_metric = run_config->add_metric_identifiers();
  third_metric->set_project_id(102);
  third_metric->set_metric_id(202);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  ASSERT_THAT(validated_context, IsOk());

  EXPECT_THAT(validated_context->run_config().metric_identifiers_size(), Eq(3));
}

TEST(VerifySignedRunContextTest, NegativeMinEpochIndexReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->set_min_epoch_index(-1);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, MaxEpochLessThanMinEpochReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->set_min_epoch_index(50);
  run_context.mutable_run_config()->set_max_epoch_index(20);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, MinEpochEqualsMaxEpochSucceeds) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->set_min_epoch_index(15);
  run_context.mutable_run_config()->set_max_epoch_index(15);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  ASSERT_THAT(validated_context, IsOk());

  EXPECT_THAT(validated_context->run_config().min_epoch_index(), Eq(15));
  EXPECT_THAT(validated_context->run_config().max_epoch_index(), Eq(15));
}

TEST(VerifySignedRunContextTest, ZeroTargetBatchSizeReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->set_target_batch_size(0);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, NegativeTargetBatchSizeReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->set_target_batch_size(-5);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::string(kValidSignature));

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, StringViewOverloadSucceedsForValidInput) {
  const SignedRunContext signed_context = CreateValidSignedRunContext();
  const std::string serialized = signed_context.SerializeAsString();

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(serialized);

  ASSERT_THAT(validated_context, IsOk());

  EXPECT_THAT(validated_context->run_id(), Eq(std::string(32, 'a')));
}

TEST(VerifySignedRunContextTest, StringViewOverloadFailsForEmptyOrCorruptString) {
  EXPECT_THAT(VerifySignedRunContext(""), StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(VerifySignedRunContext("invalid_not_proto"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, StandaloneVerifyRunContext) {
  const RunContext valid_context = CreateValidRunContext();
  EXPECT_THAT(VerifyRunContext(valid_context), IsOk());

  RunContext invalid_context = valid_context;
  invalid_context.clear_run_id();
  EXPECT_THAT(VerifyRunContext(invalid_context), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, StandaloneVerifyRunConfig) {
  const RunConfig valid_config = CreateValidRunConfig();
  EXPECT_THAT(VerifyRunConfig(valid_config), IsOk());

  RunConfig invalid_config = valid_config;
  invalid_config.set_target_batch_size(0);
  EXPECT_THAT(VerifyRunConfig(invalid_config), StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
