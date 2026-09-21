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

#include "src/containers/key_releaser/key_releaser_fn.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "gtest/gtest.h"
#include "src/containers/base/counter_names.h"
#include "src/crypto/multiset_fingerprint.h"
#include "src/crypto/release_encrypter.h"
#include "src/crypto/verify_signed_run_context.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/release.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/fake_context.h"
#include "src/testing/test_utils.h"
#include "tink/aead.h"
#include "tink/aead/aead_config.h"
#include "tink/config/global_registry.h"
#include "tink/insecure_secret_key_access.h"
#include "tink/keyset_handle.h"
#include "tink/proto_keyset_format.h"
#include "tink/util/secret_data.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cobalt::trusted_shuffler::testing::CreateMetricEpochKey;
using ::cobalt::trusted_shuffler::testing::FakeCombinedContext;
using ::cobalt::trusted_shuffler::testing::MetricEpochSpec;
using ::cobalt::trusted_shuffler::testing::PadRunId;
using ::confidential_federated_compute::Session;
using FakeContext = ::cobalt::trusted_shuffler::testing::FakeCombinedContext;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::SizeIs;

using PartitionSpec = MetricEpochSpec;

struct MessageSpec {
  PartitionSpec partition;
  std::string customer_message;
};

ShuffledMessage CreateShuffledMessage(const MessageSpec& spec) {
  ShuffledMessage msg;
  *msg.mutable_metric_epoch_key() = CreateMetricEpochKey(spec.partition);
  msg.set_customer_message(spec.customer_message);
  msg.set_is_padding(false);
  return msg;
}

SignedRunContext CreateSignedRunContext(absl::string_view run_id_prefix,
                                        absl::string_view release_key) {
  RunContext run_context;
  run_context.set_run_id(PadRunId(run_id_prefix));
  run_context.set_run_hash_key(std::string(kRunHashKeyByteLength, 'h'));
  run_context.set_release_key(std::string(release_key));

  auto* run_config = run_context.mutable_run_config();
  run_config->set_min_epoch_index(0);
  run_config->set_max_epoch_index(200);
  run_config->set_target_batch_size(100);
  auto* metric_id1 = run_config->add_metric_identifiers();
  metric_id1->set_project_id(1);
  metric_id1->set_metric_id(2);
  auto* metric_id2 = run_config->add_metric_identifiers();
  metric_id2->set_project_id(1);
  metric_id2->set_metric_id(3);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature("valid-signature");
  return signed_context;
}

IngestionVerificationPayload CreateVerificationPayload(
    const SignedRunContext& signed_context, absl::Span<const PartitionSpec> approved,
    absl::Span<const MessageSpec> ingested_messages) {
  RunContext run_context;
  run_context.ParseFromString(signed_context.serialized_run_context());

  MultisetFingerprint ingestion_fp(Stage::STAGE_INGESTION, /*is_partial=*/false, run_context,
                                   MultisetFingerprint::kFingerprintSize);
  for (const auto& msg_spec : ingested_messages) {
    ingestion_fp.AddMessage(CreateShuffledMessage(msg_spec));
  }

  IngestionVerificationPayload payload;
  auto* result = payload.mutable_success();
  *result->mutable_signed_run_context() = signed_context;
  auto* threshold_report = result->mutable_threshold_report();
  threshold_report->set_run_id(run_context.run_id());
  for (const auto& spec : approved) {
    *threshold_report->add_approved_partitions() = CreateMetricEpochKey(spec);
  }
  *result->mutable_canonical_fingerprint() = ingestion_fp.Build();
  return payload;
}

PartialReleaseReport CreatePartialReleaseReport(const RunContext& run_context,
                                                absl::Span<const MessageSpec> worker_messages) {
  MultisetFingerprint worker_fp(Stage::STAGE_RELEASE, /*is_partial=*/true, run_context,
                                MultisetFingerprint::kFingerprintSize);
  for (const auto& msg_spec : worker_messages) {
    worker_fp.AddMessage(CreateShuffledMessage(msg_spec));
  }
  PartialReleaseReport report;
  *report.mutable_partial_fingerprint() = worker_fp.Build();
  return report;
}

Session::KV CreateRecord(absl::string_view serialized_payload) {
  return Session::KV(google::protobuf::Any(), std::string(serialized_payload));
}

class KeyReleaserFnTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { ASSERT_THAT(crypto::tink::AeadConfig::Register(), IsOk()); }
};

TEST_F(KeyReleaserFnTest, SingleWorkerHappyPathReleasesKey) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context = CreateSignedRunContext("run-single-worker", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const MessageSpec messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-2"},
  };

  const IngestionVerificationPayload config_payload =
      CreateVerificationPayload(signed_context, approved, messages);
  ASSERT_THAT(fn.Do(CreateRecord(config_payload.SerializeAsString()), context), IsOk());
  EXPECT_TRUE(fn.HasConfig());
  EXPECT_FALSE(fn.HasIntegrityFault());

  const PartialReleaseReport report = CreatePartialReleaseReport(run_context, messages);
  ASSERT_THAT(fn.Do(CreateRecord(report.SerializeAsString()), context), IsOk());
  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());
  EXPECT_TRUE(fn.IsFinalized());

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.Errors(), IsEmpty());

  EXPECT_THAT(context.Unencrypted()[0].key.type_url(), IsEmpty());

  DecryptionKeyRelease key_release;
  ASSERT_TRUE(key_release.ParseFromString(context.Unencrypted()[0].data));
  ASSERT_TRUE(key_release.has_released_key_report());
  EXPECT_THAT(key_release.released_key_report().release_key(), Eq(*release_key));

  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kUnencryptedOutputRecordCount)), Eq(1));
}

TEST_F(KeyReleaserFnTest, MultiWorkerHappyPathReleasesKey) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context = CreateSignedRunContext("run-multi-worker", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const PartitionSpec approved[] = {
      {.project_id = 1, .metric_id = 2, .epoch_index = 100},
      {.project_id = 1, .metric_id = 3, .epoch_index = 200},
  };
  const MessageSpec all_messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-a"},
      {.partition = {.project_id = 1, .metric_id = 3, .epoch_index = 200},
       .customer_message = "msg-b"},
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-c"},
  };
  const MessageSpec worker1_messages[] = {all_messages[0], all_messages[1]};
  const MessageSpec worker2_messages[] = {all_messages[2]};

  const IngestionVerificationPayload config_payload =
      CreateVerificationPayload(signed_context, approved, all_messages);
  ASSERT_THAT(fn.Do(CreateRecord(config_payload.SerializeAsString()), context), IsOk());

  ASSERT_THAT(
      fn.Do(CreateRecord(
                CreatePartialReleaseReport(run_context, worker1_messages).SerializeAsString()),
            context),
      IsOk());
  ASSERT_THAT(
      fn.Do(CreateRecord(
                CreatePartialReleaseReport(run_context, worker2_messages).SerializeAsString()),
            context),
      IsOk());

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  DecryptionKeyRelease key_release;
  ASSERT_TRUE(key_release.ParseFromString(context.Unencrypted()[0].data));
  ASSERT_TRUE(key_release.has_released_key_report());
  EXPECT_THAT(key_release.released_key_report().release_key(), Eq(*release_key));
}

TEST_F(KeyReleaserFnTest, EndToEndCryptographicDecryptionWithReleasedKeySucceeds) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context = CreateSignedRunContext("run-e2e-decrypt", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const absl::StatusOr<ReleaseEncrypter> encrypter = ReleaseEncrypter::Create(run_context);
  ASSERT_THAT(encrypter, IsOk());

  const absl::string_view plaintext_customer_message = "confidential-telemetry-payload";
  const absl::StatusOr<std::string> ciphertext =
      encrypter->EncryptForRelease(plaintext_customer_message, /*associated_data=*/"");
  ASSERT_THAT(ciphertext, IsOk());

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const MessageSpec messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = std::string(plaintext_customer_message)},
  };

  ASSERT_THAT(
      fn.Do(CreateRecord(
                CreateVerificationPayload(signed_context, approved, messages).SerializeAsString()),
            context),
      IsOk());
  ASSERT_THAT(
      fn.Do(CreateRecord(CreatePartialReleaseReport(run_context, messages).SerializeAsString()),
            context),
      IsOk());
  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  DecryptionKeyRelease key_release;
  ASSERT_TRUE(key_release.ParseFromString(context.Unencrypted()[0].data));
  ASSERT_TRUE(key_release.has_released_key_report());

  const absl::StatusOr<crypto::tink::KeysetHandle> released_keyset_handle =
      crypto::tink::ParseKeysetFromProtoKeysetFormat(
          key_release.released_key_report().release_key(),
          crypto::tink::InsecureSecretKeyAccess::Get());
  ASSERT_THAT(released_keyset_handle, IsOk());

  const absl::StatusOr<std::unique_ptr<crypto::tink::Aead>> aead =
      released_keyset_handle->GetPrimitive<crypto::tink::Aead>(
          crypto::tink::ConfigGlobalRegistry());
  ASSERT_THAT(aead, IsOk());

  const absl::StatusOr<std::string> decrypted = (*aead)->Decrypt(*ciphertext, "");
  ASSERT_THAT(decrypted, IsOk());
  EXPECT_THAT(*decrypted, Eq(plaintext_customer_message));
}

TEST_F(KeyReleaserFnTest, EmptyApprovedPartitionsSucceedsAndReleasesKey) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context =
      CreateSignedRunContext("run-empty-approved", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const IngestionVerificationPayload config_payload =
      CreateVerificationPayload(signed_context, /*approved=*/{}, /*ingested_messages=*/{});
  ASSERT_THAT(fn.Do(CreateRecord(config_payload.SerializeAsString()), context), IsOk());

  const PartialReleaseReport empty_report =
      CreatePartialReleaseReport(run_context, /*worker_messages=*/{});
  ASSERT_THAT(fn.Do(CreateRecord(empty_report.SerializeAsString()), context), IsOk());
  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  DecryptionKeyRelease key_release;
  ASSERT_TRUE(key_release.ParseFromString(context.Unencrypted()[0].data));
  ASSERT_TRUE(key_release.has_released_key_report());
  EXPECT_THAT(key_release.released_key_report().release_key(), Eq(*release_key));
}

TEST_F(KeyReleaserFnTest, TamperedMessageCountEmitsIntegrityFaultAndNeverReleasesKey) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context =
      CreateSignedRunContext("run-tampered-count", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const MessageSpec messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
  };

  ASSERT_THAT(
      fn.Do(CreateRecord(
                CreateVerificationPayload(signed_context, approved, messages).SerializeAsString()),
            context),
      IsOk());

  PartialReleaseReport report = CreatePartialReleaseReport(run_context, messages);
  report.mutable_partial_fingerprint()->mutable_entries(0)->set_message_count(2);
  ASSERT_THAT(fn.Do(CreateRecord(report.SerializeAsString()), context), IsOk());

  EXPECT_THAT(
      fn.FinalizeReplica(google::protobuf::Any(), context),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "Release multiset fingerprint does not match canonical ingestion fingerprint."));

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  EXPECT_THAT(context.Unencrypted()[0].key.type_url(), IsEmpty());

  DecryptionKeyRelease key_release;
  ASSERT_TRUE(key_release.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_TRUE(key_release.has_integrity_fault());
  EXPECT_FALSE(key_release.has_released_key_report());
  EXPECT_THAT(context.Unencrypted()[0].data, Not(HasSubstr(*release_key)));

  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kUnencryptedOutputRecordCount)), Eq(1));
}

TEST_F(KeyReleaserFnTest, TamperedFingerprintHashEmitsIntegrityFaultAndNeverReleasesKey) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context = CreateSignedRunContext("run-tampered-hash", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const MessageSpec messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
  };

  ASSERT_THAT(
      fn.Do(CreateRecord(
                CreateVerificationPayload(signed_context, approved, messages).SerializeAsString()),
            context),
      IsOk());

  PartialReleaseReport report = CreatePartialReleaseReport(run_context, messages);
  std::string* fp_bytes =
      report.mutable_partial_fingerprint()->mutable_entries(0)->mutable_fingerprint();
  (*fp_bytes)[0] ^= 0xFF;
  ASSERT_THAT(fn.Do(CreateRecord(report.SerializeAsString()), context), IsOk());

  EXPECT_THAT(
      fn.FinalizeReplica(google::protobuf::Any(), context),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "Release multiset fingerprint does not match canonical ingestion fingerprint."));

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  DecryptionKeyRelease key_release;
  ASSERT_TRUE(key_release.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_TRUE(key_release.has_integrity_fault());
  EXPECT_FALSE(key_release.has_released_key_report());
}

TEST_F(KeyReleaserFnTest, MissingApprovedPartitionEmitsIntegrityFault) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context =
      CreateSignedRunContext("run-missing-partition", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const PartitionSpec approved[] = {
      {.project_id = 1, .metric_id = 2, .epoch_index = 100},
      {.project_id = 1, .metric_id = 3, .epoch_index = 200},
  };
  const MessageSpec ingested_messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
      {.partition = {.project_id = 1, .metric_id = 3, .epoch_index = 200},
       .customer_message = "msg-2"},
  };
  const MessageSpec partial_worker_messages[] = {ingested_messages[0]};

  ASSERT_THAT(
      fn.Do(CreateRecord(CreateVerificationPayload(signed_context, approved, ingested_messages)
                             .SerializeAsString()),
            context),
      IsOk());
  ASSERT_THAT(fn.Do(CreateRecord(CreatePartialReleaseReport(run_context, partial_worker_messages)
                                     .SerializeAsString()),
                    context),
              IsOk());

  EXPECT_THAT(
      fn.FinalizeReplica(google::protobuf::Any(), context),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "Release multiset fingerprint does not match canonical ingestion fingerprint."));
}

TEST_F(KeyReleaserFnTest, ExtraUnapprovedPartitionEmitsIntegrityFault) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context =
      CreateSignedRunContext("run-extra-partition", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const PartitionSpec approved[] = {
      {.project_id = 1, .metric_id = 2, .epoch_index = 100},
  };
  const MessageSpec ingested_messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
  };
  const MessageSpec extra_worker_messages[] = {
      ingested_messages[0],
      {.partition = {.project_id = 1, .metric_id = 3, .epoch_index = 200},
       .customer_message = "msg-extra"},
  };

  ASSERT_THAT(
      fn.Do(CreateRecord(CreateVerificationPayload(signed_context, approved, ingested_messages)
                             .SerializeAsString()),
            context),
      IsOk());
  EXPECT_THAT(
      fn.Do(CreateRecord(
                CreatePartialReleaseReport(run_context, extra_worker_messages).SerializeAsString()),
            context),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "Invalid PartialReleaseReport in KeyReleaserFn."));
  EXPECT_TRUE(fn.HasIntegrityFault());

  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  DecryptionKeyRelease key_release;
  ASSERT_TRUE(key_release.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_TRUE(key_release.has_integrity_fault());
}

TEST_F(KeyReleaserFnTest, UpstreamIntegrityFaultInRecord0EmitsIntegrityFaultOnFinalize) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  IngestionVerificationPayload fault_payload;
  fault_payload.mutable_integrity_fault();
  EXPECT_THAT(fn.Do(CreateRecord(fault_payload.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_TRUE(fn.HasIntegrityFault());

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  DecryptionKeyRelease key_release;
  ASSERT_TRUE(key_release.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_TRUE(key_release.has_integrity_fault());

  EXPECT_THAT(
      fn.FinalizeReplica(google::protobuf::Any(), context),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "Release verification failed due to missing configuration or integrity fault."));
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
}

TEST_F(KeyReleaserFnTest, CorruptOrInvalidPartialReleaseReportReturnsInvalidArgument) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context =
      CreateSignedRunContext("run-corrupt-report", *release_key);

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const MessageSpec messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
  };

  ASSERT_THAT(
      fn.Do(CreateRecord(
                CreateVerificationPayload(signed_context, approved, messages).SerializeAsString()),
            context),
      IsOk());

  EXPECT_THAT(fn.Do(CreateRecord("unparseable-binary-garbage"), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(KeyReleaserFnTest, MissingPartialFingerprintInReportTriggersIntegrityFault) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context =
      CreateSignedRunContext("run-missing-partial-fp", *release_key);

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const MessageSpec messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
  };

  ASSERT_THAT(
      fn.Do(CreateRecord(
                CreateVerificationPayload(signed_context, approved, messages).SerializeAsString()),
            context),
      IsOk());

  PartialReleaseReport empty_report;
  EXPECT_THAT(fn.Do(CreateRecord(empty_report.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_TRUE(fn.HasIntegrityFault());

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  DecryptionKeyRelease key_release;
  ASSERT_TRUE(key_release.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_TRUE(key_release.has_integrity_fault());

  EXPECT_THAT(
      fn.FinalizeReplica(google::protobuf::Any(), context),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "Release verification failed due to missing configuration or integrity fault."));
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
}

TEST_F(KeyReleaserFnTest, WrongStageIdInPartialReleaseReportTriggersIntegrityFault) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context =
      CreateSignedRunContext("run-wrong-stage-id", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const MessageSpec messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
  };

  ASSERT_THAT(
      fn.Do(CreateRecord(
                CreateVerificationPayload(signed_context, approved, messages).SerializeAsString()),
            context),
      IsOk());

  PartialReleaseReport report = CreatePartialReleaseReport(run_context, messages);
  report.mutable_partial_fingerprint()->set_stage_id(Stage::STAGE_INGESTION);
  EXPECT_THAT(fn.Do(CreateRecord(report.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_TRUE(fn.HasIntegrityFault());

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  DecryptionKeyRelease key_release;
  ASSERT_TRUE(key_release.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_TRUE(key_release.has_integrity_fault());

  EXPECT_THAT(
      fn.FinalizeReplica(google::protobuf::Any(), context),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "Release verification failed due to missing configuration or integrity fault."));
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
}

TEST_F(KeyReleaserFnTest, NonPartialFingerprintInReportTriggersIntegrityFault) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context =
      CreateSignedRunContext("run-non-partial-fp", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const MessageSpec messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
  };

  ASSERT_THAT(
      fn.Do(CreateRecord(
                CreateVerificationPayload(signed_context, approved, messages).SerializeAsString()),
            context),
      IsOk());

  PartialReleaseReport report = CreatePartialReleaseReport(run_context, messages);
  report.mutable_partial_fingerprint()->set_is_partial(false);
  EXPECT_THAT(fn.Do(CreateRecord(report.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_TRUE(fn.HasIntegrityFault());

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  DecryptionKeyRelease key_release;
  ASSERT_TRUE(key_release.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_TRUE(key_release.has_integrity_fault());

  EXPECT_THAT(
      fn.FinalizeReplica(google::protobuf::Any(), context),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "Release verification failed due to missing configuration or integrity fault."));
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
}

TEST_F(KeyReleaserFnTest, FinalizeWithoutRecord0EmitsIntegrityFault) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  EXPECT_THAT(
      fn.FinalizeReplica(google::protobuf::Any(), context),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "Release verification failed due to missing configuration or integrity fault."));
}

TEST_F(KeyReleaserFnTest, DoubleFinalizeReturnsFailedPrecondition) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition, "Replica is already finalized."));
}

TEST_F(KeyReleaserFnTest, DoAfterFinalizeReturnsFailedPrecondition) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context =
      CreateSignedRunContext("run-after-finalize", *release_key);
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const MessageSpec messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
  };

  EXPECT_THAT(
      fn.Do(CreateRecord(
                CreateVerificationPayload(signed_context, approved, messages).SerializeAsString()),
            context),
      StatusIs(absl::StatusCode::kFailedPrecondition, "Replica is already finalized."));
}

TEST_F(KeyReleaserFnTest, NeverLeaksSecretsInErrorMessagesOrFaultOutputs) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context = CreateSignedRunContext("secret-run-id-xyz", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const MessageSpec messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
  };

  ASSERT_THAT(
      fn.Do(CreateRecord(
                CreateVerificationPayload(signed_context, approved, messages).SerializeAsString()),
            context),
      IsOk());

  // Send mismatched PartialReleaseReport
  PartialReleaseReport mismatched_report = CreatePartialReleaseReport(run_context, messages);
  mismatched_report.mutable_partial_fingerprint()->mutable_entries(0)->set_message_count(999);
  ASSERT_THAT(fn.Do(CreateRecord(mismatched_report.SerializeAsString()), context), IsOk());

  const absl::Status status = fn.FinalizeReplica(google::protobuf::Any(), context);
  EXPECT_FALSE(status.ok());
  EXPECT_THAT(status.message(), Not(HasSubstr(run_context.run_id())));
  EXPECT_THAT(status.message(), Not(HasSubstr(run_context.run_hash_key())));
  EXPECT_THAT(status.message(), Not(HasSubstr(run_context.release_key())));

  for (const auto& err : context.Errors()) {
    EXPECT_THAT(err.message(), Not(HasSubstr(run_context.run_id())));
    EXPECT_THAT(err.message(), Not(HasSubstr(run_context.run_hash_key())));
    EXPECT_THAT(err.message(), Not(HasSubstr(run_context.release_key())));
  }
  for (const auto& kv : context.Unencrypted()) {
    EXPECT_THAT(kv.data, Not(HasSubstr(run_context.run_id())));
    EXPECT_THAT(kv.data, Not(HasSubstr(run_context.run_hash_key())));
    EXPECT_THAT(kv.data, Not(HasSubstr(run_context.release_key())));
  }
}

TEST_F(KeyReleaserFnTest, CorruptPartialReleaseReportInvalidFingerprintSizeTriggersIntegrityFault) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context =
      CreateSignedRunContext("run-invalid-fp-size", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const MessageSpec messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
  };

  ASSERT_THAT(
      fn.Do(CreateRecord(
                CreateVerificationPayload(signed_context, approved, messages).SerializeAsString()),
            context),
      IsOk());

  // Worker 1 sends a valid report
  PartialReleaseReport worker1_report = CreatePartialReleaseReport(run_context, messages);
  ASSERT_THAT(fn.Do(CreateRecord(worker1_report.SerializeAsString()), context), IsOk());

  // Worker 2 sends a report with message_count = 0 but a corrupt 5-byte fingerprint
  PartialReleaseReport worker2_report;
  RunFingerprint* partial_fp = worker2_report.mutable_partial_fingerprint();
  partial_fp->set_run_id(run_context.run_id());
  partial_fp->set_stage_id(Stage::STAGE_RELEASE);
  partial_fp->set_is_partial(true);
  FingerprintEntry* entry = partial_fp->add_entries();
  entry->mutable_metric_epoch_key()->set_project_id(1);
  entry->mutable_metric_epoch_key()->set_metric_id(2);
  entry->mutable_metric_epoch_key()->set_epoch_index(100);
  entry->set_message_count(0);
  entry->set_fingerprint("bad5b");
  EXPECT_THAT(fn.Do(CreateRecord(worker2_report.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  EXPECT_TRUE(fn.HasIntegrityFault());
  ASSERT_EQ(context.Unencrypted().size(), 1);
  DecryptionKeyRelease release_msg;
  ASSERT_TRUE(release_msg.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_TRUE(release_msg.has_integrity_fault());

  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  ASSERT_EQ(context.Unencrypted().size(), 1);
}

TEST_F(KeyReleaserFnTest, CorruptPartialReleaseReportMalformedEntryTriggersIntegrityFault) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context =
      CreateSignedRunContext("run-malformed-entry", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const MessageSpec messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
  };

  ASSERT_THAT(
      fn.Do(CreateRecord(
                CreateVerificationPayload(signed_context, approved, messages).SerializeAsString()),
            context),
      IsOk());

  // Send a report with duplicate partition entries within the same PartialReleaseReport
  PartialReleaseReport dup_report = CreatePartialReleaseReport(run_context, messages);
  *dup_report.mutable_partial_fingerprint()->add_entries() =
      dup_report.partial_fingerprint().entries(0);
  EXPECT_THAT(fn.Do(CreateRecord(dup_report.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));

  EXPECT_TRUE(fn.HasIntegrityFault());
  ASSERT_EQ(context.Unencrypted().size(), 1);
  DecryptionKeyRelease release_msg;
  ASSERT_TRUE(release_msg.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_TRUE(release_msg.has_integrity_fault());

  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  ASSERT_EQ(context.Unencrypted().size(), 1);
}

TEST_F(KeyReleaserFnTest, ApprovedPartitionWithZeroMessageCountSucceeds) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context =
      CreateSignedRunContext("run-zero-count-part", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const PartitionSpec approved[] = {
      {.project_id = 1, .metric_id = 2, .epoch_index = 100},
      {.project_id = 1, .metric_id = 2, .epoch_index = 101},
  };
  const MessageSpec messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
  };

  IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, approved, messages);
  // Add the second approved partition (epoch 101) with message_count = 0 to canonical_fingerprint.
  FingerprintEntry* zero_entry =
      payload.mutable_success()->mutable_canonical_fingerprint()->add_entries();
  zero_entry->mutable_metric_epoch_key()->set_project_id(1);
  zero_entry->mutable_metric_epoch_key()->set_metric_id(2);
  zero_entry->mutable_metric_epoch_key()->set_epoch_index(101);
  zero_entry->set_message_count(0);

  ASSERT_THAT(fn.Do(CreateRecord(payload.SerializeAsString()), context), IsOk());

  // Worker emits PartialReleaseReport containing only non-padding messages (epoch 100).
  PartialReleaseReport worker_report = CreatePartialReleaseReport(run_context, messages);
  ASSERT_THAT(fn.Do(CreateRecord(worker_report.SerializeAsString()), context), IsOk());

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());
  EXPECT_FALSE(fn.HasIntegrityFault());
  ASSERT_EQ(context.Unencrypted().size(), 1);

  DecryptionKeyRelease release_msg;
  ASSERT_TRUE(release_msg.ParseFromString(context.Unencrypted()[0].data));
  ASSERT_TRUE(release_msg.has_released_key_report());
  EXPECT_EQ(release_msg.released_key_report().release_key(), *release_key);
}

TEST_F(KeyReleaserFnTest,
       CorruptPartialReleaseReportNonZero32ByteFingerprintForZeroCountTriggersIntegrityFault) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context =
      CreateSignedRunContext("run-zero-count-nonzero-fp", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const MessageSpec messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
  };

  ASSERT_THAT(
      fn.Do(CreateRecord(
                CreateVerificationPayload(signed_context, approved, messages).SerializeAsString()),
            context),
      IsOk());

  PartialReleaseReport bad_report;
  RunFingerprint* partial_fp = bad_report.mutable_partial_fingerprint();
  partial_fp->set_run_id(run_context.run_id());
  partial_fp->set_stage_id(Stage::STAGE_RELEASE);
  partial_fp->set_is_partial(true);
  FingerprintEntry* entry = partial_fp->add_entries();
  entry->mutable_metric_epoch_key()->set_project_id(1);
  entry->mutable_metric_epoch_key()->set_metric_id(2);
  entry->mutable_metric_epoch_key()->set_epoch_index(100);
  entry->set_message_count(0);
  entry->set_fingerprint(std::string(32, 'x'));

  EXPECT_THAT(fn.Do(CreateRecord(bad_report.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_TRUE(fn.HasIntegrityFault());
  ASSERT_EQ(context.Unencrypted().size(), 1);
  DecryptionKeyRelease release_msg;
  ASSERT_TRUE(release_msg.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_TRUE(release_msg.has_integrity_fault());
}

TEST_F(KeyReleaserFnTest,
       PartialReleaseReportWithUnapprovedZeroCountPartitionTriggersIntegrityFault) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context =
      CreateSignedRunContext("run-unapproved-zero-part", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const MessageSpec messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
  };

  ASSERT_THAT(
      fn.Do(CreateRecord(
                CreateVerificationPayload(signed_context, approved, messages).SerializeAsString()),
            context),
      IsOk());

  PartialReleaseReport bad_report = CreatePartialReleaseReport(run_context, messages);
  FingerprintEntry* unapproved_entry = bad_report.mutable_partial_fingerprint()->add_entries();
  unapproved_entry->mutable_metric_epoch_key()->set_project_id(1);
  unapproved_entry->mutable_metric_epoch_key()->set_metric_id(2);
  unapproved_entry->mutable_metric_epoch_key()->set_epoch_index(999);
  unapproved_entry->set_message_count(0);
  unapproved_entry->set_fingerprint("");

  EXPECT_THAT(fn.Do(CreateRecord(bad_report.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_TRUE(fn.HasIntegrityFault());
  ASSERT_EQ(context.Unencrypted().size(), 1);
  DecryptionKeyRelease release_msg;
  ASSERT_TRUE(release_msg.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_TRUE(release_msg.has_integrity_fault());
}

TEST_F(KeyReleaserFnTest,
       PartialReleaseReportWithUnapprovedNonZeroPartitionTriggersIntegrityFaultInOnRecord) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context =
      CreateSignedRunContext("run-unapproved-nonzero-part", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const MessageSpec messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 100},
       .customer_message = "msg-1"},
  };

  ASSERT_THAT(
      fn.Do(CreateRecord(
                CreateVerificationPayload(signed_context, approved, messages).SerializeAsString()),
            context),
      IsOk());

  const MessageSpec unapproved_messages[] = {
      {.partition = {.project_id = 1, .metric_id = 2, .epoch_index = 999},
       .customer_message = "msg-unapproved"},
  };
  PartialReleaseReport bad_report = CreatePartialReleaseReport(run_context, unapproved_messages);

  EXPECT_THAT(fn.Do(CreateRecord(bad_report.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_TRUE(fn.HasIntegrityFault());
  ASSERT_EQ(context.Unencrypted().size(), 1);
  DecryptionKeyRelease release_msg;
  ASSERT_TRUE(release_msg.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_TRUE(release_msg.has_integrity_fault());
}

TEST_F(KeyReleaserFnTest,
       MultipleWorkersDisjointApprovedPartitionsWithZeroCountApprovedPartitionSucceeds) {
  KeyReleaserFn fn;
  FakeCombinedContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());
  const SignedRunContext signed_context =
      CreateSignedRunContext("run-disjoint-zero-count", *release_key);
  RunContext run_context;
  ASSERT_TRUE(run_context.ParseFromString(signed_context.serialized_run_context()));

  const PartitionSpec approved[] = {
      {.project_id = 1, .metric_id = 10, .epoch_index = 100},
      {.project_id = 1, .metric_id = 20, .epoch_index = 100},
      {.project_id = 1, .metric_id = 30, .epoch_index = 100},
  };
  const MessageSpec all_messages[] = {
      {.partition = {.project_id = 1, .metric_id = 10, .epoch_index = 100},
       .customer_message = "msg-10-a"},
      {.partition = {.project_id = 1, .metric_id = 10, .epoch_index = 100},
       .customer_message = "msg-10-b"},
      {.partition = {.project_id = 1, .metric_id = 20, .epoch_index = 100},
       .customer_message = "msg-20-a"},
  };

  IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, approved, all_messages);
  // Add the third approved partition (metric_id = 30) with message_count = 0.
  FingerprintEntry* zero_entry =
      payload.mutable_success()->mutable_canonical_fingerprint()->add_entries();
  zero_entry->mutable_metric_epoch_key()->set_project_id(1);
  zero_entry->mutable_metric_epoch_key()->set_metric_id(30);
  zero_entry->mutable_metric_epoch_key()->set_epoch_index(100);
  zero_entry->set_message_count(0);

  ASSERT_THAT(fn.Do(CreateRecord(payload.SerializeAsString()), context), IsOk());

  // Worker 1 processes only partition (1, 10, 100).
  const MessageSpec worker1_messages[] = {
      {.partition = {.project_id = 1, .metric_id = 10, .epoch_index = 100},
       .customer_message = "msg-10-a"},
      {.partition = {.project_id = 1, .metric_id = 10, .epoch_index = 100},
       .customer_message = "msg-10-b"},
  };
  PartialReleaseReport worker1_report = CreatePartialReleaseReport(run_context, worker1_messages);
  ASSERT_THAT(fn.Do(CreateRecord(worker1_report.SerializeAsString()), context), IsOk());

  // Worker 2 processes only partition (1, 20, 100).
  const MessageSpec worker2_messages[] = {
      {.partition = {.project_id = 1, .metric_id = 20, .epoch_index = 100},
       .customer_message = "msg-20-a"},
  };
  PartialReleaseReport worker2_report = CreatePartialReleaseReport(run_context, worker2_messages);
  ASSERT_THAT(fn.Do(CreateRecord(worker2_report.SerializeAsString()), context), IsOk());

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());
  EXPECT_FALSE(fn.HasIntegrityFault());
  ASSERT_EQ(context.Unencrypted().size(), 1);

  DecryptionKeyRelease release_msg;
  ASSERT_TRUE(release_msg.ParseFromString(context.Unencrypted()[0].data));
  ASSERT_TRUE(release_msg.has_released_key_report());
  EXPECT_EQ(release_msg.released_key_report().release_key(), *release_key);
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
