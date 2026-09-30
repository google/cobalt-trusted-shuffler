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

#include "src/containers/key_releaser/key_releaser_transform.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "containers/fns/confidential_transform_server.h"
#include "containers/fns/do_fn.h"
#include "containers/fns/fn.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "gtest/gtest.h"
#include "src/containers/key_releaser/key_releaser_fn.h"
#include "src/crypto/multiset_fingerprint.h"
#include "src/crypto/release_encrypter.h"
#include "src/crypto/verify_signed_run_context.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/release.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/fake_context.h"
#include "src/testing/fake_keys.h"
#include "src/testing/test_utils.h"
#include "tink/aead/aead_config.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using FakeContext = ::cobalt::trusted_shuffler::testing::FakeCombinedContext;
using ::cobalt::trusted_shuffler::testing::FakeEncryptionKeyHandle;
using ::cobalt::trusted_shuffler::testing::FakeSigningKey;
using ::confidential_federated_compute::fns::DoFn;
using ::confidential_federated_compute::fns::Fn;
using ::confidential_federated_compute::fns::FnConfidentialTransform;
using ::testing::Eq;
using ::testing::SizeIs;

class KeyReleaserTransformTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { ASSERT_THAT(crypto::tink::AeadConfig::Register(), IsOk()); }
};

TEST_F(KeyReleaserTransformTest, CreateKeyReleaserTransformReturnsNonNull) {
  const std::unique_ptr<FnConfidentialTransform> transform = CreateKeyReleaserTransform(
      std::make_unique<FakeSigningKey>(), std::make_unique<FakeEncryptionKeyHandle>());

  EXPECT_NE(transform, nullptr);
}

TEST_F(KeyReleaserTransformTest, KeyReleaserFnFactoryReturnsKeyReleaserFn) {
  KeyReleaserFnFactory factory;
  const absl::StatusOr<std::unique_ptr<Fn>> fn = factory.CreateFn();
  ASSERT_THAT(fn, IsOk());

  const KeyReleaserFn* const key_releaser_fn = dynamic_cast<KeyReleaserFn*>(fn.value().get());
  ASSERT_NE(key_releaser_fn, nullptr);
}

TEST_F(KeyReleaserTransformTest, FnFactoryCreatedFnExecutesLifecycleAndReleasesKey) {
  KeyReleaserFnFactory factory;
  absl::StatusOr<std::unique_ptr<Fn>> fn_or = factory.CreateFn();
  ASSERT_THAT(fn_or, IsOk());
  std::unique_ptr<Fn> fn = std::move(*fn_or);

  FakeContext context;
  ASSERT_THAT(fn->InitializeReplica(google::protobuf::Any(), context), IsOk());

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());

  RunContext run_context;
  run_context.set_run_id(std::string(kRunIdByteLength, 'r'));
  run_context.set_run_hash_key(std::string(kRunHashKeyByteLength, 'h'));
  run_context.set_release_key(*release_key);
  auto* run_config = run_context.mutable_run_config();
  run_config->set_min_epoch_index(0);
  run_config->set_max_epoch_index(100);
  run_config->set_target_batch_size(50);
  auto* metric_id = run_config->add_metric_identifiers();
  metric_id->set_project_id(1);
  metric_id->set_metric_id(2);

  SignedRunContext signed_context = testing::CreateValidSignedRunContext(run_context);

  MultisetFingerprint ingestion_fp(Stage::STAGE_INGESTION, /*is_partial=*/false, run_context,
                                   MultisetFingerprint::kFingerprintSize);
  IngestionVerificationPayload config_payload;
  auto* result = config_payload.mutable_success();
  *result->mutable_signed_run_context() = signed_context;
  result->mutable_threshold_report()->set_run_id(run_context.run_id());
  *result->mutable_canonical_fingerprint() = ingestion_fp.Build();

  auto* const do_fn = dynamic_cast<DoFn*>(fn.get());
  ASSERT_NE(do_fn, nullptr);
  ASSERT_THAT(do_fn->Do(DoFn::KV(config_payload.SerializeAsString()), context), IsOk());

  MultisetFingerprint release_fp(Stage::STAGE_RELEASE, /*is_partial=*/true, run_context,
                                 MultisetFingerprint::kFingerprintSize);
  PartialReleaseReport report;
  *report.mutable_partial_fingerprint() = release_fp.Build();

  ASSERT_THAT(do_fn->Do(DoFn::KV(report.SerializeAsString()), context), IsOk());
  ASSERT_THAT(fn->FinalizeReplica(google::protobuf::Any(), context), IsOk());
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));

  DecryptionKeyRelease key_release;
  ASSERT_TRUE(key_release.ParseFromString(context.Unencrypted()[0].data));
  ASSERT_TRUE(key_release.has_released_key_report());
  EXPECT_THAT(key_release.released_key_report().release_key(), Eq(*release_key));
}

TEST_F(KeyReleaserTransformTest, FnFactoryCreatedFnFinalizeReplicaSucceedsWhenDoIsNeverCalled) {
  KeyReleaserFnFactory factory;
  absl::StatusOr<std::unique_ptr<Fn>> fn_or = factory.CreateFn();
  ASSERT_THAT(fn_or, IsOk());
  std::unique_ptr<Fn> fn = std::move(*fn_or);

  FakeContext context;
  ASSERT_THAT(fn->InitializeReplica(google::protobuf::Any(), context), IsOk());
  EXPECT_THAT(fn->FinalizeReplica(google::protobuf::Any(), context), IsOk());
  EXPECT_THAT(context.Unencrypted(), SizeIs(0));
  EXPECT_THAT(context.Encrypted(), SizeIs(0));
}

TEST_F(KeyReleaserTransformTest, FnFactoryCreatedFnEmitsIntegrityFaultWhenConfigIsInvalid) {
  KeyReleaserFnFactory factory;
  absl::StatusOr<std::unique_ptr<Fn>> fn_or = factory.CreateFn();
  ASSERT_THAT(fn_or, IsOk());
  std::unique_ptr<Fn> fn = std::move(*fn_or);

  FakeContext context;
  ASSERT_THAT(fn->InitializeReplica(google::protobuf::Any(), context), IsOk());
  auto* const do_fn = dynamic_cast<DoFn*>(fn.get());
  ASSERT_NE(do_fn, nullptr);
  EXPECT_THAT(do_fn->Do(DoFn::KV("not-a-valid-verification-payload"), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(fn->FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  DecryptionKeyRelease key_release;
  ASSERT_TRUE(key_release.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_TRUE(key_release.has_integrity_fault());
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
