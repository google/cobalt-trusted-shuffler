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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "gtest/gtest.h"
#include "src/crypto/release_encrypter.h"
#include "src/crypto/verify_signed_run_context.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/release.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/fake_context.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::SizeIs;

inline constexpr std::size_t kIvSize = 12;
inline constexpr std::size_t kTagSize = 16;

struct PartitionSpec {
  int32_t project_id;
  int32_t metric_id;
  int64_t epoch_index;
};

std::string PadRunId(absl::string_view id) {
  if (id.empty()) {
    return "";
  }
  std::string padded(id);
  if (padded.size() < kRunIdByteLength) {
    padded.resize(kRunIdByteLength, '_');
  }
  return padded;
}

MetricEpochKey CreateMetricEpochKey(const PartitionSpec& spec) {
  MetricEpochKey key;
  key.set_project_id(spec.project_id);
  key.set_metric_id(spec.metric_id);
  key.set_epoch_index(spec.epoch_index);
  return key;
}

SignedRunContext CreateSignedRunContext(absl::string_view run_id) {
  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  RunContext run_context;
  run_context.set_run_id(PadRunId(run_id));
  run_context.set_run_hash_key(std::string(kRunHashKeyByteLength, 'k'));
  run_context.set_release_key(*release_key);

  RunConfig* const run_config = run_context.mutable_run_config();
  run_config->set_min_epoch_index(0);
  run_config->set_max_epoch_index(1000);
  run_config->set_target_batch_size(100);
  RunConfig::MetricIdentifier* const metric = run_config->add_metric_identifiers();
  metric->set_project_id(1);
  metric->set_metric_id(2);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature("valid-signature");
  return signed_context;
}

std::string ExtractReleaseKey(const SignedRunContext& signed_context) {
  RunContext run_context;
  if (!run_context.ParseFromString(signed_context.serialized_run_context())) {
    return "";
  }
  return run_context.release_key();
}

IngestionVerificationPayload CreateVerificationPayload(
    const SignedRunContext& signed_context, absl::string_view run_id,
    absl::Span<const PartitionSpec> approved_partitions) {
  IngestionVerificationPayload payload;
  IngestionVerificationResult* const result = payload.mutable_success();
  *result->mutable_signed_run_context() = signed_context;
  RunFingerprint* const fingerprint = result->mutable_canonical_fingerprint();
  fingerprint->set_run_id(PadRunId(run_id));
  fingerprint->set_stage_id(STAGE_INGESTION);
  fingerprint->set_is_partial(false);
  auto* report = result->mutable_threshold_report();
  report->set_run_id(PadRunId(run_id));
  for (const auto& spec : approved_partitions) {
    *report->add_approved_partitions() = CreateMetricEpochKey(spec);
  }
  return payload;
}

ShuffledMessage CreateShuffledMessage(const PartitionSpec& spec, absl::string_view run_id,
                                      int32_t shuffle_count, bool is_padding,
                                      absl::string_view customer_message) {
  ShuffledMessage message;
  *message.mutable_metric_epoch_key() = CreateMetricEpochKey(spec);
  message.set_run_id(PadRunId(run_id));
  message.set_shuffle_count(shuffle_count);
  message.set_is_padding(is_padding);
  message.set_customer_message(std::string(customer_message));
  return message;
}

EncryptOutputFn::KV MakeConfigRecord(const IngestionVerificationPayload& payload) {
  return EncryptOutputFn::KV(google::protobuf::Any(), payload.SerializeAsString());
}

EncryptOutputFn::KV MakeMessageRecord(const ShuffledMessage& message) {
  return EncryptOutputFn::KV(google::protobuf::Any(), message.SerializeAsString());
}

absl::StatusOr<std::string> DecryptWithReleaseKey(absl::string_view release_key,
                                                  absl::string_view ciphertext) {
  RunContext run_context;
  run_context.set_release_key(std::string(release_key));
  const absl::StatusOr<ReleaseEncrypter> encrypter = ReleaseEncrypter::Create(run_context);
  if (!encrypter.ok()) {
    return encrypter.status();
  }
  return encrypter->DecryptReleased(ciphertext, /*associated_data=*/"");
}

void ExpectIntegrityFaultEmitted(const testing::FakeContext& context,
                                 absl::string_view expected_message) {
  ASSERT_THAT(context.Unencrypted(), Not(IsEmpty()));
  ReleaseOutputHeader header;
  ASSERT_TRUE(context.Unencrypted().back().key.UnpackTo(&header));
  EXPECT_EQ(header.type(), ReleaseOutputHeader::TYPE_INTEGRITY_FAULT);
  ASSERT_THAT(context.Errors(), Not(IsEmpty()));
  EXPECT_EQ(context.Errors().back().message(), expected_message);
}

using EncryptOutputFnTest = ::testing::Test;

TEST_F(EncryptOutputFnTest, FailsIfFirstRecordIsEmpty) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  EXPECT_THAT(fn.Do(EncryptOutputFn::KV(google::protobuf::Any(), ""), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());
}

TEST_F(EncryptOutputFnTest, FailsIfFirstRecordNotIngestionVerificationPayload) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  EXPECT_THAT(fn.Do(EncryptOutputFn::KV(google::protobuf::Any(), "corrupted-payload"), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());
}

TEST_F(EncryptOutputFnTest, FailsIfIngestionVerificationPayloadHasIntegrityFault) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  IngestionVerificationPayload payload;
  payload.mutable_integrity_fault();
  EXPECT_THAT(fn.Do(MakeConfigRecord(payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Ingestion verification failed with integrity fault."));
  ExpectIntegrityFaultEmitted(
      context, "IntegrityFault: Ingestion verification failed with integrity fault.");
  EXPECT_FALSE(fn.HasConfig());
}

TEST_F(EncryptOutputFnTest, FailsIfIngestionVerificationPayloadMissingSuccessResult) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  // Non-empty valid protobuf wire bytes with an unknown field (tag 15, varint 1)
  // so FirstRecordConfigDoFn parses it as an IngestionVerificationPayload with
  // neither `success` nor `integrity_fault` populated.
  EXPECT_THAT(fn.Do(EncryptOutputFn::KV(google::protobuf::Any(), "\x78\x01"), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: IngestionVerificationPayload missing success result."));
  ExpectIntegrityFaultEmitted(
      context, "IntegrityFault: IngestionVerificationPayload missing success result.");
  EXPECT_FALSE(fn.HasConfig());
}

TEST_F(EncryptOutputFnTest, FailsIfIngestionVerificationResultMissingSignedRunContext) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  IngestionVerificationPayload payload;
  payload.mutable_success()->mutable_threshold_report()->set_run_id(PadRunId("run-123"));
  EXPECT_THAT(fn.Do(MakeConfigRecord(payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: IngestionVerificationResult missing signed_run_context."));
  ExpectIntegrityFaultEmitted(
      context, "IntegrityFault: IngestionVerificationResult missing signed_run_context.");
  EXPECT_FALSE(fn.HasConfig());
}

TEST_F(EncryptOutputFnTest, FailsIfRunContextMissingOrEmptyRunId) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "", approved);
  EXPECT_THAT(fn.Do(MakeConfigRecord(payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Failed to verify SignedRunContext."));
  ExpectIntegrityFaultEmitted(context, "IntegrityFault: Failed to verify SignedRunContext.");
  EXPECT_FALSE(fn.HasConfig());
}

TEST_F(EncryptOutputFnTest, FailsIfRunContextPayloadCorrupted) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context("invalid-run-context-bytes");
  signed_context.set_signature("valid-signature");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-123", approved);
  EXPECT_THAT(fn.Do(MakeConfigRecord(payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Failed to verify SignedRunContext."));
  ExpectIntegrityFaultEmitted(context, "IntegrityFault: Failed to verify SignedRunContext.");
  EXPECT_FALSE(fn.HasConfig());
}

TEST_F(EncryptOutputFnTest, FailsIfRunContextMissingSignature) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  SignedRunContext signed_context = CreateSignedRunContext("run-123");
  signed_context.clear_signature();
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-123", approved);
  EXPECT_THAT(fn.Do(MakeConfigRecord(payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Failed to verify SignedRunContext."));
  ExpectIntegrityFaultEmitted(context, "IntegrityFault: Failed to verify SignedRunContext.");
  EXPECT_FALSE(fn.HasConfig());
}

TEST_F(EncryptOutputFnTest, FailsIfRunContextMissingReleaseKey) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  RunContext run_context;
  run_context.set_run_id(PadRunId("run-123"));
  run_context.set_run_hash_key(std::string(kRunHashKeyByteLength, 'k'));
  RunConfig* const run_config = run_context.mutable_run_config();
  run_config->set_min_epoch_index(0);
  run_config->set_max_epoch_index(1000);
  run_config->set_target_batch_size(100);
  RunConfig::MetricIdentifier* const metric = run_config->add_metric_identifiers();
  metric->set_project_id(1);
  metric->set_metric_id(2);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature("valid-signature");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-123", approved);
  EXPECT_THAT(fn.Do(MakeConfigRecord(payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Failed to verify SignedRunContext."));
  ExpectIntegrityFaultEmitted(context, "IntegrityFault: Failed to verify SignedRunContext.");
  EXPECT_FALSE(fn.HasConfig());
}

TEST_F(EncryptOutputFnTest, FailsIfRunContextInvalidReleaseKeyset) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  RunContext run_context;
  run_context.set_run_id(PadRunId("run-123"));
  run_context.set_run_hash_key(std::string(kRunHashKeyByteLength, 'k'));
  run_context.set_release_key("not-a-valid-tink-keyset");
  RunConfig* const run_config = run_context.mutable_run_config();
  run_config->set_min_epoch_index(0);
  run_config->set_max_epoch_index(1000);
  run_config->set_target_batch_size(100);
  RunConfig::MetricIdentifier* const metric = run_config->add_metric_identifiers();
  metric->set_project_id(1);
  metric->set_metric_id(2);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature("valid-signature");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-123", approved);
  EXPECT_THAT(fn.Do(MakeConfigRecord(payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Failed to verify SignedRunContext."));
  ExpectIntegrityFaultEmitted(context, "IntegrityFault: Failed to verify SignedRunContext.");
  EXPECT_FALSE(fn.HasConfig());
}

TEST_F(EncryptOutputFnTest, FailsIfRunContextMissingRunConfig) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  RunContext run_context;
  run_context.set_run_id(PadRunId("run-123"));
  run_context.set_run_hash_key(std::string(kRunHashKeyByteLength, 'k'));
  run_context.set_release_key(*release_key);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature("valid-signature");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-123", approved);
  EXPECT_THAT(fn.Do(MakeConfigRecord(payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Failed to verify SignedRunContext."));
  ExpectIntegrityFaultEmitted(context, "IntegrityFault: Failed to verify SignedRunContext.");
  EXPECT_FALSE(fn.HasConfig());
}

TEST_F(EncryptOutputFnTest, FailsIfIngestionVerificationResultMissingCanonicalFingerprint) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-100", approved);
  payload.mutable_success()->clear_canonical_fingerprint();
  EXPECT_THAT(
      fn.Do(MakeConfigRecord(payload), context),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "IntegrityFault: IngestionVerificationResult missing canonical_fingerprint."));
  ExpectIntegrityFaultEmitted(
      context, "IntegrityFault: IngestionVerificationResult missing canonical_fingerprint.");
  EXPECT_FALSE(fn.HasConfig());
}

TEST_F(EncryptOutputFnTest, FailsIfCanonicalFingerprintRunIdMismatch) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("expected-run-id");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "expected-run-id", approved);
  payload.mutable_success()->mutable_canonical_fingerprint()->set_run_id(PadRunId("wrong-run-id"));

  EXPECT_THAT(fn.Do(MakeConfigRecord(payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: RunFingerprint run_id mismatch."));
  ExpectIntegrityFaultEmitted(context, "IntegrityFault: RunFingerprint run_id mismatch.");
}

TEST_F(EncryptOutputFnTest, FailsIfIngestionVerificationResultMissingThresholdReport) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-100", approved);
  payload.mutable_success()->clear_threshold_report();
  EXPECT_THAT(fn.Do(MakeConfigRecord(payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: IngestionVerificationResult missing threshold_report."));
  ExpectIntegrityFaultEmitted(
      context, "IntegrityFault: IngestionVerificationResult missing threshold_report.");
  EXPECT_FALSE(fn.HasConfig());
}

TEST_F(EncryptOutputFnTest, FailsIfThresholdReportRunIdMismatch) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("expected-run-id");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "expected-run-id", approved);
  payload.mutable_success()->mutable_threshold_report()->set_run_id(PadRunId("wrong-run-id"));

  EXPECT_THAT(fn.Do(MakeConfigRecord(payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: ThresholdReport run_id mismatch."));
  ExpectIntegrityFaultEmitted(context, "IntegrityFault: ThresholdReport run_id mismatch.");
}

TEST_F(EncryptOutputFnTest, FailsIfApprovedPartitionInvalidProjectId) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec invalid_approved[] = {{.project_id = 0, .metric_id = 2, .epoch_index = 100}};
  const IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-100", invalid_approved);

  EXPECT_THAT(fn.Do(MakeConfigRecord(payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Invalid MetricEpochKey in approved_partitions."));
  ExpectIntegrityFaultEmitted(context,
                              "IntegrityFault: Invalid MetricEpochKey in approved_partitions.");
}

TEST_F(EncryptOutputFnTest, FailsIfApprovedPartitionZeroMetricId) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec invalid_approved[] = {{.project_id = 1, .metric_id = 0, .epoch_index = 100}};
  const IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-100", invalid_approved);

  EXPECT_THAT(fn.Do(MakeConfigRecord(payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Invalid MetricEpochKey in approved_partitions."));
  ExpectIntegrityFaultEmitted(context,
                              "IntegrityFault: Invalid MetricEpochKey in approved_partitions.");
}

TEST_F(EncryptOutputFnTest, FailsIfApprovedPartitionNegativeEpochIndex) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec invalid_approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = -1}};
  const IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-100", invalid_approved);

  EXPECT_THAT(fn.Do(MakeConfigRecord(payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Invalid MetricEpochKey in approved_partitions."));
  ExpectIntegrityFaultEmitted(context,
                              "IntegrityFault: Invalid MetricEpochKey in approved_partitions.");
}

TEST_F(EncryptOutputFnTest, SuccessfulConfigTransitionToConfigured) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-abc-123");
  const PartitionSpec approved[] = {
      {.project_id = 1, .metric_id = 2, .epoch_index = 100},
      {.project_id = 1, .metric_id = 3, .epoch_index = 200},
  };
  const IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-abc-123", approved);

  ASSERT_THAT(fn.Do(MakeConfigRecord(payload), context), IsOk());
  EXPECT_TRUE(fn.HasConfig());
}

TEST_F(EncryptOutputFnTest, ProcessRecordWithMalformedShuffledMessagePayloadTriggersFault) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  ASSERT_THAT(
      fn.Do(MakeConfigRecord(CreateVerificationPayload(signed_context, "run-100", approved)),
            context),
      IsOk());

  EXPECT_THAT(
      fn.Do(EncryptOutputFn::KV(google::protobuf::Any(), "not-a-valid-shuffled-message"), context),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "IntegrityFault: Failed to parse input data as ShuffledMessage."));
  ExpectIntegrityFaultEmitted(context,
                              "IntegrityFault: Failed to parse input data as ShuffledMessage.");
}

TEST_F(EncryptOutputFnTest, ProcessMessageWithInvalidShuffleCountTriggersFault) {
  const int32_t invalid_counts[] = {0, 1, 3};
  for (const int32_t count : invalid_counts) {
    EncryptOutputFn fn;
    testing::FakeContext context;

    const SignedRunContext signed_context = CreateSignedRunContext("run-100");
    const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
    ASSERT_THAT(
        fn.Do(MakeConfigRecord(CreateVerificationPayload(signed_context, "run-100", approved)),
              context),
        IsOk());

    const ShuffledMessage message = CreateShuffledMessage(
        {.project_id = 1, .metric_id = 2, .epoch_index = 100}, "run-100",
        /*shuffle_count=*/count, /*is_padding=*/false, "secret-customer-message");

    EXPECT_THAT(fn.Do(MakeMessageRecord(message), context),
                StatusIs(absl::StatusCode::kFailedPrecondition,
                         "IntegrityFault: Message shuffle_count must be exactly 2."));
    ExpectIntegrityFaultEmitted(context,
                                "IntegrityFault: Message shuffle_count must be exactly 2.");
  }
}

TEST_F(EncryptOutputFnTest, ProcessMessageWithRunIdMismatchTriggersFault) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  ASSERT_THAT(
      fn.Do(MakeConfigRecord(CreateVerificationPayload(signed_context, "run-100", approved)),
            context),
      IsOk());

  const ShuffledMessage message = CreateShuffledMessage(
      {.project_id = 1, .metric_id = 2, .epoch_index = 100}, "different-run-id",
      /*shuffle_count=*/2, /*is_padding=*/false, "secret-customer-message");

  EXPECT_THAT(
      fn.Do(MakeMessageRecord(message), context),
      StatusIs(absl::StatusCode::kFailedPrecondition, "IntegrityFault: Message run_id mismatch."));
  ExpectIntegrityFaultEmitted(context, "IntegrityFault: Message run_id mismatch.");
}

TEST_F(EncryptOutputFnTest, ProcessMessageMissingMetricEpochKeyTriggersFault) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  ASSERT_THAT(
      fn.Do(MakeConfigRecord(CreateVerificationPayload(signed_context, "run-100", approved)),
            context),
      IsOk());

  ShuffledMessage message;
  message.set_run_id(PadRunId("run-100"));
  message.set_shuffle_count(2);
  message.set_is_padding(false);
  message.set_customer_message("secret-customer-message");

  EXPECT_THAT(fn.Do(MakeMessageRecord(message), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Message missing metric_epoch_key."));
  ExpectIntegrityFaultEmitted(context, "IntegrityFault: Message missing metric_epoch_key.");
}

TEST_F(EncryptOutputFnTest, ProcessMessageWithUnapprovedPartitionTriggersFault) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  ASSERT_THAT(
      fn.Do(MakeConfigRecord(CreateVerificationPayload(signed_context, "run-100", approved)),
            context),
      IsOk());

  const ShuffledMessage message =
      CreateShuffledMessage({.project_id = 1, .metric_id = 999, .epoch_index = 100}, "run-100",
                            /*shuffle_count=*/2, /*is_padding=*/false, "secret-customer-message");

  EXPECT_THAT(fn.Do(MakeMessageRecord(message), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Message partition not in approved partitions."));
  ExpectIntegrityFaultEmitted(context,
                              "IntegrityFault: Message partition not in approved partitions.");
}

TEST_F(EncryptOutputFnTest, ProcessValidMessageEncryptsPayloadAndEmitsUnencrypted) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const std::string release_key = ExtractReleaseKey(signed_context);
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  ASSERT_THAT(
      fn.Do(MakeConfigRecord(CreateVerificationPayload(signed_context, "run-100", approved)),
            context),
      IsOk());

  const std::string plaintext_payload = "super-confidential-user-telemetry-data";
  const ShuffledMessage message =
      CreateShuffledMessage({.project_id = 1, .metric_id = 2, .epoch_index = 100}, "run-100",
                            /*shuffle_count=*/2, /*is_padding=*/false, plaintext_payload);

  ASSERT_THAT(fn.Do(MakeMessageRecord(message), context), IsOk());

  // Verify emission via EmitUnencrypted.
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  ReleaseOutputHeader header;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&header));
  EXPECT_EQ(header.type(), ReleaseOutputHeader::TYPE_MESSAGE);

  // Verify unencrypted payload does NOT contain plaintext (privacy guarantee).
  EXPECT_THAT(context.Unencrypted()[0].data, Not(HasSubstr(plaintext_payload)));

  // Parse EncryptedMessageOutput and decrypt with worker release key.
  EncryptedMessageOutput output;
  ASSERT_TRUE(output.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_EQ(output.metric_epoch_key().project_id(), 1);
  EXPECT_EQ(output.metric_epoch_key().metric_id(), 2);
  EXPECT_EQ(output.metric_epoch_key().epoch_index(), 100);
  EXPECT_EQ(output.encrypted_message().size(), kIvSize + plaintext_payload.size() + kTagSize);

  const auto decrypted = DecryptWithReleaseKey(release_key, output.encrypted_message());
  ASSERT_THAT(decrypted, IsOk());
  EXPECT_EQ(*decrypted, plaintext_payload);
}

TEST_F(EncryptOutputFnTest, ProcessMessageWithEmptyPayloadSucceeds) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const std::string release_key = ExtractReleaseKey(signed_context);
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  ASSERT_THAT(
      fn.Do(MakeConfigRecord(CreateVerificationPayload(signed_context, "run-100", approved)),
            context),
      IsOk());

  const ShuffledMessage message =
      CreateShuffledMessage({.project_id = 1, .metric_id = 2, .epoch_index = 100}, "run-100",
                            /*shuffle_count=*/2, /*is_padding=*/false, /*customer_message=*/"");

  ASSERT_THAT(fn.Do(MakeMessageRecord(message), context), IsOk());

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  EncryptedMessageOutput output;
  ASSERT_TRUE(output.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_EQ(output.encrypted_message().size(), kIvSize + kTagSize);

  const auto decrypted = DecryptWithReleaseKey(release_key, output.encrypted_message());
  ASSERT_THAT(decrypted, IsOk());
  EXPECT_EQ(*decrypted, "");
}

TEST_F(EncryptOutputFnTest, ProcessPaddingMessageDoesNotEmitAndIsIgnored) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  ASSERT_THAT(
      fn.Do(MakeConfigRecord(CreateVerificationPayload(signed_context, "run-100", approved)),
            context),
      IsOk());

  // Padding message.
  const ShuffledMessage padding_msg =
      CreateShuffledMessage({.project_id = 1, .metric_id = 2, .epoch_index = 100}, "run-100",
                            /*shuffle_count=*/2, /*is_padding=*/true, "dummy-padding-payload");
  ASSERT_THAT(fn.Do(MakeMessageRecord(padding_msg), context), IsOk());

  // Verify no messages were emitted for padding.
  EXPECT_THAT(context.Unencrypted(), IsEmpty());

  // Real message.
  const ShuffledMessage real_msg =
      CreateShuffledMessage({.project_id = 1, .metric_id = 2, .epoch_index = 100}, "run-100",
                            /*shuffle_count=*/2, /*is_padding=*/false, "real-customer-message");
  ASSERT_THAT(fn.Do(MakeMessageRecord(real_msg), context), IsOk());
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));

  // Finalize replica and verify multiset fingerprint only counted 1 message.
  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());
  ASSERT_THAT(context.Encrypted(), SizeIs(1));
  PartialReleaseReport partial_report;
  ASSERT_TRUE(partial_report.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_EQ(partial_report.partial_fingerprint().entries_size(), 1);
  EXPECT_EQ(partial_report.partial_fingerprint().entries(0).message_count(), 1);
}

TEST_F(EncryptOutputFnTest, FinalizeReplicaBeforeConfigTriggersFault) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  EXPECT_THAT(
      fn.FinalizeReplica(google::protobuf::Any(), context),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "IntegrityFault: Configuration was not established before FinalizeReplica."));
  ExpectIntegrityFaultEmitted(
      context, "IntegrityFault: Configuration was not established before FinalizeReplica.");
  EXPECT_THAT(context.Encrypted(), IsEmpty());
}

TEST_F(EncryptOutputFnTest, FinalizeReplicaWithActiveFaultDoesNotEmitReport) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  ASSERT_THAT(
      fn.Do(MakeConfigRecord(CreateVerificationPayload(signed_context, "run-100", approved)),
            context),
      IsOk());

  const ShuffledMessage invalid_msg =
      CreateShuffledMessage({.project_id = 1, .metric_id = 999, .epoch_index = 100}, "run-100",
                            /*shuffle_count=*/2, /*is_padding=*/false, "payload");
  ASSERT_THAT(fn.Do(MakeMessageRecord(invalid_msg), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Message partition not in approved partitions."));

  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition, "IntegrityFault"));
  ExpectIntegrityFaultEmitted(context, "IntegrityFault");
  EXPECT_THAT(context.Encrypted(), IsEmpty());
}

TEST_F(EncryptOutputFnTest, FinalizeReplicaEmitsPartialReleaseReportEncryptedUnderReencryptionKey) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {
      {.project_id = 1, .metric_id = 2, .epoch_index = 100},
      {.project_id = 1, .metric_id = 3, .epoch_index = 200},
  };
  ASSERT_THAT(
      fn.Do(MakeConfigRecord(CreateVerificationPayload(signed_context, "run-100", approved)),
            context),
      IsOk());

  // Message 1.
  const ShuffledMessage msg1 =
      CreateShuffledMessage({.project_id = 1, .metric_id = 2, .epoch_index = 100}, "run-100",
                            /*shuffle_count=*/2, /*is_padding=*/false, "msg-1");
  ASSERT_THAT(fn.Do(MakeMessageRecord(msg1), context), IsOk());

  // Message 2.
  const ShuffledMessage msg2 =
      CreateShuffledMessage({.project_id = 1, .metric_id = 3, .epoch_index = 200}, "run-100",
                            /*shuffle_count=*/2, /*is_padding=*/false, "msg-2");
  ASSERT_THAT(fn.Do(MakeMessageRecord(msg2), context), IsOk());

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  // Verify EmitEncrypted at key index 0 (kReencryptionKeyIndex).
  ASSERT_THAT(context.Encrypted(), SizeIs(1));
  EXPECT_EQ(context.Encrypted()[0].first, EncryptOutputFn::kReencryptionKeyIndex);

  ReleaseOutputHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_EQ(header.type(), ReleaseOutputHeader::TYPE_REPORT);

  PartialReleaseReport partial_report;
  ASSERT_TRUE(partial_report.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_TRUE(partial_report.partial_fingerprint().is_partial());
  EXPECT_EQ(partial_report.partial_fingerprint().stage_id(), STAGE_RELEASE);
  EXPECT_EQ(partial_report.partial_fingerprint().entries_size(), 2);
}

TEST_F(EncryptOutputFnTest, FinalizeReplicaTwiceFailsPrecondition) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  ASSERT_THAT(
      fn.Do(MakeConfigRecord(CreateVerificationPayload(signed_context, "run-100", approved)),
            context),
      IsOk());

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());
  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition, "Replica is already finalized."));
}

TEST_F(EncryptOutputFnTest, FinalizeReplicaWithZeroMessagesSucceeds) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  ASSERT_THAT(
      fn.Do(MakeConfigRecord(CreateVerificationPayload(signed_context, "run-100", approved)),
            context),
      IsOk());

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());
  ASSERT_THAT(context.Encrypted(), SizeIs(1));
  PartialReleaseReport partial_report;
  ASSERT_TRUE(partial_report.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_EQ(partial_report.partial_fingerprint().entries_size(), 0);
}

TEST_F(EncryptOutputFnTest, SubsequentRecordsAndFinalizeLogGenericIntegrityFault) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  ASSERT_THAT(
      fn.Do(MakeConfigRecord(CreateVerificationPayload(signed_context, "run-100", approved)),
            context),
      IsOk());

  // First fault logs/returns specific reason with "IntegrityFault: " prefix.
  const ShuffledMessage bad_msg =
      CreateShuffledMessage({.project_id = 1, .metric_id = 999, .epoch_index = 100}, "run-100",
                            /*shuffle_count=*/2, /*is_padding=*/false, "payload");
  EXPECT_THAT(fn.Do(MakeMessageRecord(bad_msg), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: Message partition not in approved partitions."));
  ExpectIntegrityFaultEmitted(context,
                              "IntegrityFault: Message partition not in approved partitions.");

  // Subsequent records log/return generic "IntegrityFault".
  const ShuffledMessage next_msg =
      CreateShuffledMessage({.project_id = 1, .metric_id = 2, .epoch_index = 100}, "run-100",
                            /*shuffle_count=*/2, /*is_padding=*/false, "payload");
  EXPECT_THAT(fn.Do(MakeMessageRecord(next_msg), context),
              StatusIs(absl::StatusCode::kFailedPrecondition, "IntegrityFault"));
  ExpectIntegrityFaultEmitted(context, "IntegrityFault");

  // FinalizeReplica also logs/returns generic "IntegrityFault".
  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition, "IntegrityFault"));
  ExpectIntegrityFaultEmitted(context, "IntegrityFault");
}

TEST_F(EncryptOutputFnTest, FinalizeReplicaAfterConfigFaultLogsGenericIntegrityFault) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("expected-run-id");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const IngestionVerificationPayload bad_payload =
      CreateVerificationPayload(signed_context, "wrong-run-id", approved);

  // First fault occurs during OnConfig with "IntegrityFault: " prefix.
  EXPECT_THAT(fn.Do(MakeConfigRecord(bad_payload), context),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "IntegrityFault: ThresholdReport run_id mismatch."));
  ExpectIntegrityFaultEmitted(context, "IntegrityFault: ThresholdReport run_id mismatch.");

  // Subsequent FinalizeReplica call logs/returns generic "IntegrityFault".
  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition, "IntegrityFault"));
  ExpectIntegrityFaultEmitted(context, "IntegrityFault");
}

TEST_F(EncryptOutputFnTest,
       FinalizeReplicaBeforeConfigThenSubsequentFinalizeLogsGenericIntegrityFault) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  // First FinalizeReplica before config logs/returns specific fault reason.
  EXPECT_THAT(
      fn.FinalizeReplica(google::protobuf::Any(), context),
      StatusIs(absl::StatusCode::kFailedPrecondition,
               "IntegrityFault: Configuration was not established before FinalizeReplica."));
  ExpectIntegrityFaultEmitted(
      context, "IntegrityFault: Configuration was not established before FinalizeReplica.");

  // Second FinalizeReplica call logs/returns generic "IntegrityFault".
  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kFailedPrecondition, "IntegrityFault"));
  ExpectIntegrityFaultEmitted(context, "IntegrityFault");
}

TEST_F(EncryptOutputFnTest, ProcessRecordAfterFinalizeReplicaFailsPrecondition) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  ASSERT_THAT(
      fn.Do(MakeConfigRecord(CreateVerificationPayload(signed_context, "run-100", approved)),
            context),
      IsOk());

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  const ShuffledMessage msg =
      CreateShuffledMessage({.project_id = 1, .metric_id = 2, .epoch_index = 100}, "run-100",
                            /*shuffle_count=*/2, /*is_padding=*/false, "post-finalize-message");
  EXPECT_THAT(fn.Do(MakeMessageRecord(msg), context),
              StatusIs(absl::StatusCode::kFailedPrecondition, "Replica is already finalized."));
}

TEST_F(EncryptOutputFnTest, DeduplicatesApprovedPartitionsAndProcessesMessages) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved_with_duplicates[] = {
      {.project_id = 1, .metric_id = 2, .epoch_index = 100},
      {.project_id = 1, .metric_id = 2, .epoch_index = 100},
      {.project_id = 1, .metric_id = 3, .epoch_index = 200},
  };
  ASSERT_THAT(fn.Do(MakeConfigRecord(CreateVerificationPayload(signed_context, "run-100",
                                                               approved_with_duplicates)),
                    context),
              IsOk());

  const ShuffledMessage msg1 =
      CreateShuffledMessage({.project_id = 1, .metric_id = 2, .epoch_index = 100}, "run-100",
                            /*shuffle_count=*/2, /*is_padding=*/false, "msg-1");
  ASSERT_THAT(fn.Do(MakeMessageRecord(msg1), context), IsOk());

  const ShuffledMessage msg2 =
      CreateShuffledMessage({.project_id = 1, .metric_id = 3, .epoch_index = 200}, "run-100",
                            /*shuffle_count=*/2, /*is_padding=*/false, "msg-2");
  ASSERT_THAT(fn.Do(MakeMessageRecord(msg2), context), IsOk());
  EXPECT_THAT(context.Unencrypted(), SizeIs(2));
}

TEST_F(EncryptOutputFnTest, PrivacyBoundaryEnforcedSecretsNeverLeakedToOrchestrator) {
  EncryptOutputFn fn;
  testing::FakeContext context;

  const std::string run_id = PadRunId("secret-run-identifier-123");
  const SignedRunContext signed_context = CreateSignedRunContext("secret-run-identifier-123");
  RunContext parsed_context;
  ASSERT_TRUE(parsed_context.ParseFromString(signed_context.serialized_run_context()));
  const std::string run_hash_key = parsed_context.run_hash_key();
  const std::string release_key = parsed_context.release_key();

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  ASSERT_THAT(
      fn.Do(MakeConfigRecord(CreateVerificationPayload(signed_context, run_id, approved)), context),
      IsOk());

  const ShuffledMessage valid_msg =
      CreateShuffledMessage({.project_id = 1, .metric_id = 2, .epoch_index = 100}, run_id,
                            /*shuffle_count=*/2, /*is_padding=*/false, "customer-payload");
  ASSERT_THAT(fn.Do(MakeMessageRecord(valid_msg), context), IsOk());

  // Trigger a run_id mismatch fault on subsequent message.
  const ShuffledMessage bad_msg =
      CreateShuffledMessage({.project_id = 1, .metric_id = 2, .epoch_index = 100}, "other-run-id",
                            /*shuffle_count=*/2, /*is_padding=*/false, "customer-payload");
  const absl::Status fault_status = fn.Do(MakeMessageRecord(bad_msg), context);
  EXPECT_THAT(fault_status, StatusIs(absl::StatusCode::kFailedPrecondition));

  // Returned status message must never contain secrets.
  EXPECT_THAT(fault_status.message(), Not(HasSubstr(run_id)));
  EXPECT_THAT(fault_status.message(), Not(HasSubstr(run_hash_key)));
  EXPECT_THAT(fault_status.message(), Not(HasSubstr(release_key)));

  // Context error messages must never contain secrets.
  for (const absl::Status& err : context.Errors()) {
    EXPECT_THAT(err.message(), Not(HasSubstr(run_id)));
    EXPECT_THAT(err.message(), Not(HasSubstr(run_hash_key)));
    EXPECT_THAT(err.message(), Not(HasSubstr(release_key)));
  }

  // Unencrypted emissions (headers and payloads) must never contain secrets.
  for (const auto& kv : context.Unencrypted()) {
    EXPECT_THAT(kv.data, Not(HasSubstr(run_id)));
    EXPECT_THAT(kv.data, Not(HasSubstr(run_hash_key)));
    EXPECT_THAT(kv.data, Not(HasSubstr(release_key)));
  }
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
