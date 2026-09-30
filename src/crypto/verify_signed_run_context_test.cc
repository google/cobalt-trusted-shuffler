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

#include <memory>
#include <string>

#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "proto/attestation/eventlog.pb.h"
#include "proto/attestation/evidence.pb.h"
#include "proto/attestation/verification.pb.h"
#include "proto/session/messages.pb.h"
#include "src/crypto/release_encrypter.h"
#include "src/crypto/secure_random.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/test_utils.h"
#include "tink/aead/aead_key_templates.h"
#include "tink/config/global_registry.h"
#include "tink/insecure_secret_key_access.h"
#include "tink/keyset_handle.h"
#include "tink/proto_keyset_format.h"
#include "tink/secret_data.h"
#include "tink/util/secret_data.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Not;

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
  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ABSL_CHECK_OK(release_key.status());

  RunContext context;
  context.set_run_id(std::string(kRunIdByteLength, 'a'));
  context.set_run_hash_key(std::string(kRunHashKeyByteLength, 'b'));
  *context.mutable_run_config() = CreateValidRunConfig();
  context.set_release_key(*release_key);
  return context;
}

oak::session::v1::EndorsedEvidence CreateEndorsedEvidenceWithSigningKey(
    absl::string_view signing_key_bytes) {
  oak::attestation::v1::ContainerLayerData container_layer_data;
  container_layer_data.set_signing_public_key(std::string(signing_key_bytes));
  google::protobuf::Any any;
  any.PackFrom(container_layer_data);
  oak::attestation::v1::Event event;
  event.set_tag("ORCHESTRATOR");
  *event.mutable_event() = any;

  oak::session::v1::EndorsedEvidence evidence;
  oak::attestation::v1::Event stage0_event;
  stage0_event.set_tag("Stage0");
  oak::attestation::v1::Event system_event;
  system_event.set_tag("stage1");

  evidence.mutable_evidence()->mutable_event_log()->add_encoded_events(
      stage0_event.SerializeAsString());
  evidence.mutable_evidence()->mutable_event_log()->add_encoded_events(
      system_event.SerializeAsString());
  evidence.mutable_evidence()->mutable_event_log()->add_encoded_events(event.SerializeAsString());
  return evidence;
}

SignedRunContext CreateValidSignedRunContext(const RunContext& run_context) {
  return testing::CreateValidSignedRunContext(run_context);
}

SignedRunContext CreateValidSignedRunContext() {
  return CreateValidSignedRunContext(CreateValidRunContext());
}

TEST(VerifySignedRunContextTest, ValidSignedRunContextSucceeds) {
  const SignedRunContext signed_context = CreateValidSignedRunContext();

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  ASSERT_THAT(validated_context, IsOk());

  EXPECT_THAT(validated_context->run_id(), Eq(std::string(kRunIdByteLength, 'a')));
  EXPECT_THAT(validated_context->run_hash_key(), Eq(std::string(kRunHashKeyByteLength, 'b')));
  EXPECT_THAT(validated_context->release_key(), Not(IsEmpty()));
  EXPECT_THAT(ReleaseEncrypter::Create(*validated_context), IsOk());
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
  SignedRunContext signed_context = CreateValidSignedRunContext();
  signed_context.clear_serialized_run_context();

  EXPECT_THAT(VerifySignedRunContext(signed_context), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, MissingEndorsedEvidenceReturnsInvalidArgument) {
  SignedRunContext signed_context = CreateValidSignedRunContext();
  signed_context.clear_endorsed_evidence();

  EXPECT_THAT(VerifySignedRunContext(signed_context), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, EmptyEvidenceInEndorsedEvidenceReturnsInvalidArgument) {
  SignedRunContext signed_context = CreateValidSignedRunContext();
  signed_context.mutable_endorsed_evidence()->clear_evidence();

  EXPECT_THAT(VerifySignedRunContext(signed_context), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, NoSigningKeyInEvidenceReturnsInvalidArgument) {
  SignedRunContext signed_context = CreateValidSignedRunContext();
  signed_context.mutable_endorsed_evidence()->mutable_evidence()->clear_event_log();

  EXPECT_THAT(VerifySignedRunContext(signed_context), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, InvalidSignatureLengthReturnsInvalidArgument) {
  SignedRunContext signed_context = CreateValidSignedRunContext();
  signed_context.set_signature(std::string(63, 'x'));
  EXPECT_THAT(VerifySignedRunContext(signed_context), StatusIs(absl::StatusCode::kInvalidArgument));

  signed_context.set_signature(std::string(65, 'x'));
  EXPECT_THAT(VerifySignedRunContext(signed_context), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, CorruptedSignatureReturnsInvalidArgument) {
  SignedRunContext signed_context = CreateValidSignedRunContext();
  std::string corrupted_sig = signed_context.signature();
  corrupted_sig[0] ^= 0xFF;
  signed_context.set_signature(corrupted_sig);

  EXPECT_THAT(VerifySignedRunContext(signed_context), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, MismatchedKeyReturnsInvalidArgument) {
  testing::TestSigningKey key1;
  testing::TestSigningKey key2;
  const RunContext run_context = CreateValidRunContext();

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(key1.Sign(signed_context.serialized_run_context()));
  *signed_context.mutable_endorsed_evidence() = key2.EndorsedEvidence();

  EXPECT_THAT(VerifySignedRunContext(signed_context), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, ExtractPublicKeyWithMultipleEventsSucceeds) {
  testing::TestSigningKey signing_key;
  oak::session::v1::EndorsedEvidence endorsed_evidence;

  // Add an unrelated event first.
  oak::attestation::v1::Event unrelated_event;
  unrelated_event.set_tag("unrelated-event-tag");
  unrelated_event.mutable_event()->set_value("some-data");
  endorsed_evidence.mutable_evidence()->mutable_event_log()->add_encoded_events(
      unrelated_event.SerializeAsString());

  // Add a malformed event string (should be skipped gracefully).
  endorsed_evidence.mutable_evidence()->mutable_event_log()->add_encoded_events(
      "not-a-valid-serialized-event-proto");

  // Add the valid signing public key event.
  oak::attestation::v1::ContainerLayerData layer_data;
  layer_data.set_signing_public_key(signing_key.UncompressedPublicKeyBytes());
  google::protobuf::Any any;
  any.PackFrom(layer_data);
  oak::attestation::v1::Event key_event;
  key_event.set_tag("ORCHESTRATOR");
  *key_event.mutable_event() = any;
  endorsed_evidence.mutable_evidence()->mutable_event_log()->add_encoded_events(
      key_event.SerializeAsString());

  const RunContext run_context = CreateValidRunContext();
  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(signing_key.Sign(signed_context.serialized_run_context()));
  *signed_context.mutable_endorsed_evidence() = endorsed_evidence;

  EXPECT_THAT(VerifySignedRunContext(signed_context), IsOk());
}

TEST(VerifySignedRunContextTest, InvalidPublicPointNotOnCurveReturnsInvalidArgument) {
  oak::session::v1::EndorsedEvidence endorsed_evidence =
      CreateEndorsedEvidenceWithSigningKey(std::string(64, '\0'));

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context("dummy");
  signed_context.set_signature(std::string(64, 's'));
  *signed_context.mutable_endorsed_evidence() = endorsed_evidence;

  EXPECT_THAT(VerifySignedRunContext(signed_context), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, CorruptedSerializedRunContextReturnsInvalidArgument) {
  SignedRunContext signed_context;
  signed_context.set_serialized_run_context("invalid_not_a_proto_stream");
  signed_context.set_signature(
      testing::GetDefaultTestSigningKey().Sign(signed_context.serialized_run_context()));
  *signed_context.mutable_endorsed_evidence() =
      testing::GetDefaultTestSigningKey().EndorsedEvidence();

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, EmptyRunIdReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.clear_run_id();

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, ShortRunIdReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.set_run_id(std::string(kRunIdByteLength - 1, 'x'));

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, LongRunIdReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.set_run_id(std::string(kRunIdByteLength + 1, 'x'));

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, EmptyRunHashKeyReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.clear_run_hash_key();

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, ShortRunHashKeyReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.set_run_hash_key(std::string(kRunHashKeyByteLength - 1, 'y'));

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, LongRunHashKeyReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.set_run_hash_key(std::string(kRunHashKeyByteLength + 1, 'y'));

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, MissingRunConfigReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.clear_run_config();

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, EmptyMetricIdentifiersReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->clear_metric_identifiers();

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, ZeroProjectIdReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->mutable_metric_identifiers(0)->set_project_id(0);

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, NegativeProjectIdReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->mutable_metric_identifiers(0)->set_project_id(-10);

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, ZeroMetricIdReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->mutable_metric_identifiers(0)->set_metric_id(0);

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, NegativeMetricIdReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->mutable_metric_identifiers(0)->set_metric_id(-5);

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, DuplicateMetricIdentifiersReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  RunConfig::MetricIdentifier* const dup =
      run_context.mutable_run_config()->add_metric_identifiers();
  dup->set_project_id(101);
  dup->set_metric_id(202);

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, MultipleDistinctMetricIdentifiersSucceeds) {
  RunContext run_context = CreateValidRunContext();
  RunConfig::MetricIdentifier* const second =
      run_context.mutable_run_config()->add_metric_identifiers();
  second->set_project_id(101);
  second->set_metric_id(203);

  RunConfig::MetricIdentifier* const third =
      run_context.mutable_run_config()->add_metric_identifiers();
  third->set_project_id(102);
  third->set_metric_id(202);

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  ASSERT_THAT(validated_context, IsOk());

  EXPECT_THAT(validated_context->run_config().metric_identifiers_size(), Eq(3));
}

TEST(VerifySignedRunContextTest, NegativeMinEpochIndexReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->set_min_epoch_index(-1);

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, MaxEpochLessThanMinEpochReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->set_min_epoch_index(20);
  run_context.mutable_run_config()->set_max_epoch_index(19);

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, MinEpochEqualsMaxEpochSucceeds) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->set_min_epoch_index(15);
  run_context.mutable_run_config()->set_max_epoch_index(15);

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  ASSERT_THAT(validated_context, IsOk());

  EXPECT_THAT(validated_context->run_config().min_epoch_index(), Eq(15));
  EXPECT_THAT(validated_context->run_config().max_epoch_index(), Eq(15));
}

TEST(VerifySignedRunContextTest, ZeroTargetBatchSizeReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->set_target_batch_size(0);

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, NegativeTargetBatchSizeReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.mutable_run_config()->set_target_batch_size(-5);

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, StringViewOverloadSucceedsForValidInput) {
  const SignedRunContext signed_context = CreateValidSignedRunContext();
  const std::string serialized = signed_context.SerializeAsString();

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(serialized);

  ASSERT_THAT(validated_context, IsOk());

  EXPECT_THAT(validated_context->run_id(), Eq(std::string(kRunIdByteLength, 'a')));
}

TEST(VerifySignedRunContextTest, StringViewOverloadFailsForEmptyOrCorruptString) {
  EXPECT_THAT(VerifySignedRunContext(""), StatusIs(absl::StatusCode::kInvalidArgument));

  EXPECT_THAT(VerifySignedRunContext("invalid_not_proto"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, EmptyReleaseKeyReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.clear_release_key();

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, RawKeyInsteadOfTinkKeysetReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.set_release_key(std::string(32, 'x'));

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, CorruptedReleaseKeyReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  run_context.set_release_key("not a valid serialized tink keyset");

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  const absl::StatusOr<RunContext> validated_context = VerifySignedRunContext(signed_context);

  EXPECT_THAT(validated_context, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, WrongKeyTemplateReleaseKeyReturnsInvalidArgument) {
  RunContext run_context = CreateValidRunContext();
  const absl::StatusOr<std::unique_ptr<crypto::tink::KeysetHandle>> tink_prefix_handle =
      crypto::tink::KeysetHandle::GenerateNew(crypto::tink::AeadKeyTemplates::Aes256Gcm(),
                                              crypto::tink::KeyGenConfigGlobalRegistry());
  ASSERT_THAT(tink_prefix_handle, IsOk());
  const absl::StatusOr<crypto::tink::SecretData> tink_prefix_serialized =
      crypto::tink::SerializeKeysetToProtoKeysetFormat(
          **tink_prefix_handle, crypto::tink::InsecureSecretKeyAccess::Get());
  ASSERT_THAT(tink_prefix_serialized, IsOk());

  run_context.set_release_key(
      std::string(crypto::tink::util::SecretDataAsStringView(*tink_prefix_serialized)));

  const SignedRunContext signed_context = CreateValidSignedRunContext(run_context);

  EXPECT_THAT(VerifySignedRunContext(signed_context), StatusIs(absl::StatusCode::kInvalidArgument));
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

TEST(VerifySignedRunContextTest, ExtractPublicKeyFromContainerLayerDataSucceeds) {
  testing::TestSigningKey signing_key;
  const oak::session::v1::EndorsedEvidence endorsed_evidence =
      signing_key.CreateEndorsedEvidenceWithContainerLayerData();

  const RunContext run_context = CreateValidRunContext();
  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(signing_key.Sign(signed_context.serialized_run_context()));
  *signed_context.mutable_endorsed_evidence() = endorsed_evidence;

  EXPECT_THAT(VerifySignedRunContext(signed_context), IsOk());
}

TEST(VerifySignedRunContextTest, ValidSignedRunContextWith65ByteUncompressedKeySucceeds) {
  testing::TestSigningKey signing_key;
  oak::session::v1::EndorsedEvidence endorsed_evidence =
      CreateEndorsedEvidenceWithSigningKey(signing_key.UncompressedPublicKeyBytes());

  const RunContext run_context = CreateValidRunContext();
  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(signing_key.Sign(signed_context.serialized_run_context()));
  *signed_context.mutable_endorsed_evidence() = endorsed_evidence;

  EXPECT_THAT(VerifySignedRunContext(signed_context), IsOk());
}

TEST(VerifySignedRunContextTest, ValidSignedRunContextWith64ByteKeySucceeds) {
  testing::TestSigningKey signing_key;
  oak::session::v1::EndorsedEvidence endorsed_evidence =
      CreateEndorsedEvidenceWithSigningKey(signing_key.PublicKeyBytes());

  const RunContext run_context = CreateValidRunContext();
  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(signing_key.Sign(signed_context.serialized_run_context()));
  *signed_context.mutable_endorsed_evidence() = endorsed_evidence;

  EXPECT_THAT(VerifySignedRunContext(signed_context), IsOk());
}

TEST(VerifySignedRunContextTest, Invalid65ByteKeyInvalidHeaderReturnsInvalidArgument) {
  testing::TestSigningKey signing_key;
  std::string invalid_65 = signing_key.UncompressedPublicKeyBytes();
  invalid_65[0] = '\x05';  // Invalid SEC1 header for uncompressed point.

  oak::session::v1::EndorsedEvidence endorsed_evidence =
      CreateEndorsedEvidenceWithSigningKey(invalid_65);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(CreateValidRunContext().SerializeAsString());
  signed_context.set_signature(signing_key.Sign(signed_context.serialized_run_context()));
  *signed_context.mutable_endorsed_evidence() = endorsed_evidence;

  EXPECT_THAT(VerifySignedRunContext(signed_context), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VerifySignedRunContextTest, InvalidKeyLengthReturnsInvalidArgument) {
  testing::TestSigningKey signing_key;

  // 32-byte public key (invalid length)
  oak::session::v1::EndorsedEvidence evidence_32 =
      CreateEndorsedEvidenceWithSigningKey(std::string(32, 'k'));

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(CreateValidRunContext().SerializeAsString());
  signed_context.set_signature(signing_key.Sign(signed_context.serialized_run_context()));
  *signed_context.mutable_endorsed_evidence() = evidence_32;

  EXPECT_THAT(VerifySignedRunContext(signed_context), StatusIs(absl::StatusCode::kInvalidArgument));

  // 66-byte public key (invalid length)
  oak::session::v1::EndorsedEvidence evidence_66;
  oak::attestation::v1::Event event_66;
  event_66.set_tag("oak-signing-public-key:ecdsa-p256");
  event_66.mutable_event()->set_value(std::string(66, 'k'));
  evidence_66.mutable_evidence()->mutable_event_log()->add_encoded_events(
      event_66.SerializeAsString());
  *signed_context.mutable_endorsed_evidence() = evidence_66;

  EXPECT_THAT(VerifySignedRunContext(signed_context), StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
