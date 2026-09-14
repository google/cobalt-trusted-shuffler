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

#include "src/crypto/verify_ingestion_verification_payload.h"

#include <cstdint>
#include <string>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/crypto/release_encrypter.h"
#include "src/crypto/verify_signed_run_context.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::SizeIs;

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

IngestionVerificationResult CreateVerificationResult(
    const SignedRunContext& signed_context, absl::string_view run_id,
    absl::Span<const PartitionSpec> approved_partitions) {
  IngestionVerificationResult result;
  *result.mutable_signed_run_context() = signed_context;
  auto* report = result.mutable_threshold_report();
  report->set_run_id(PadRunId(run_id));
  for (const auto& spec : approved_partitions) {
    *report->add_approved_partitions() = CreateMetricEpochKey(spec);
  }
  return result;
}

IngestionVerificationPayload CreateVerificationPayload(
    const SignedRunContext& signed_context, absl::string_view run_id,
    absl::Span<const PartitionSpec> approved_partitions) {
  IngestionVerificationPayload payload;
  *payload.mutable_success() =
      CreateVerificationResult(signed_context, run_id, approved_partitions);
  RunFingerprint* const canonical_fp = payload.mutable_success()->mutable_canonical_fingerprint();
  canonical_fp->set_run_id(PadRunId(run_id));
  canonical_fp->set_stage_id(STAGE_INGESTION);
  canonical_fp->set_is_partial(false);
  return payload;
}

TEST(VerifyIngestionVerificationPayloadTest, ValidVerificationPayloadSucceeds) {
  const SignedRunContext signed_context = CreateSignedRunContext("run-abc-123");
  const PartitionSpec approved[] = {
      {.project_id = 1, .metric_id = 2, .epoch_index = 0},
      {.project_id = 1, .metric_id = 3, .epoch_index = 200},
  };
  const IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-abc-123", approved);

  const absl::StatusOr<VerifiedIngestionVerificationResult> verified =
      VerifyIngestionVerificationPayload(payload);
  ASSERT_THAT(verified, IsOk());
  EXPECT_EQ(verified->run_context.run_id(), PadRunId("run-abc-123"));
  EXPECT_THAT(verified->approved_partitions, SizeIs(2));
  EXPECT_TRUE(verified->approved_partitions.contains(
      CreateMetricEpochKey({.project_id = 1, .metric_id = 2, .epoch_index = 0})));
  EXPECT_TRUE(verified->approved_partitions.contains(
      CreateMetricEpochKey({.project_id = 1, .metric_id = 3, .epoch_index = 200})));
  EXPECT_EQ(verified->canonical_fingerprint.run_id(), PadRunId("run-abc-123"));
  EXPECT_EQ(verified->canonical_fingerprint.stage_id(), STAGE_INGESTION);
  EXPECT_FALSE(verified->canonical_fingerprint.is_partial());
}

TEST(VerifyIngestionVerificationPayloadTest, DeduplicatesApprovedPartitions) {
  const SignedRunContext signed_context = CreateSignedRunContext("run-abc-123");
  const PartitionSpec approved_with_duplicates[] = {
      {.project_id = 1, .metric_id = 2, .epoch_index = 100},
      {.project_id = 1, .metric_id = 2, .epoch_index = 100},
      {.project_id = 1, .metric_id = 3, .epoch_index = 200},
  };
  const IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-abc-123", approved_with_duplicates);

  const absl::StatusOr<VerifiedIngestionVerificationResult> verified =
      VerifyIngestionVerificationPayload(payload);
  ASSERT_THAT(verified, IsOk());
  EXPECT_THAT(verified->approved_partitions, SizeIs(2));
}

TEST(VerifyIngestionVerificationPayloadTest, EmptyApprovedPartitionsSucceeds) {
  const SignedRunContext signed_context = CreateSignedRunContext("run-abc-123");
  const IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-abc-123", {});

  const absl::StatusOr<VerifiedIngestionVerificationResult> verified =
      VerifyIngestionVerificationPayload(payload);
  ASSERT_THAT(verified, IsOk());
  EXPECT_THAT(verified->approved_partitions, IsEmpty());
}

TEST(VerifyIngestionVerificationPayloadTest, FailsWhenPayloadHasIntegrityFault) {
  IngestionVerificationPayload payload;
  payload.mutable_integrity_fault();

  EXPECT_THAT(VerifyIngestionVerificationPayload(payload),
              StatusIs(absl::StatusCode::kFailedPrecondition,
                       "Ingestion verification failed with integrity fault."));
}

TEST(VerifyIngestionVerificationPayloadTest, FailsWhenPayloadMissingSuccessResult) {
  IngestionVerificationPayload payload;

  EXPECT_THAT(VerifyIngestionVerificationPayload(payload),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "IngestionVerificationPayload missing success result."));
}

TEST(VerifyIngestionVerificationPayloadTest, FailsWhenMissingSignedRunContext) {
  IngestionVerificationPayload payload;
  payload.mutable_success()->mutable_threshold_report()->set_run_id(PadRunId("run-123"));

  EXPECT_THAT(VerifyIngestionVerificationPayload(payload),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "IngestionVerificationResult missing signed_run_context."));
}

TEST(VerifyIngestionVerificationPayloadTest, FailsWhenSignedRunContextCorrupted) {
  SignedRunContext signed_context;
  signed_context.set_serialized_run_context("corrupted-bytes");
  signed_context.set_signature("valid-signature");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-123", approved);

  EXPECT_THAT(VerifyIngestionVerificationPayload(payload),
              StatusIs(absl::StatusCode::kInvalidArgument, "Failed to verify SignedRunContext."));
}

TEST(VerifyIngestionVerificationPayloadTest, FailsWhenSignedRunContextMissingSignature) {
  SignedRunContext signed_context = CreateSignedRunContext("run-123");
  signed_context.clear_signature();
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-123", approved);

  EXPECT_THAT(VerifyIngestionVerificationPayload(payload),
              StatusIs(absl::StatusCode::kInvalidArgument, "Failed to verify SignedRunContext."));
}

TEST(VerifyIngestionVerificationPayloadTest, FailsWhenMissingThresholdReport) {
  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-100", approved);
  payload.mutable_success()->clear_threshold_report();

  EXPECT_THAT(VerifyIngestionVerificationPayload(payload),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "IngestionVerificationResult missing threshold_report."));
}

TEST(VerifyIngestionVerificationPayloadTest, FailsWhenThresholdReportRunIdMismatch) {
  const SignedRunContext signed_context = CreateSignedRunContext("expected-run-id");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "expected-run-id", approved);
  payload.mutable_success()->mutable_threshold_report()->set_run_id(PadRunId("wrong-run-id"));

  EXPECT_THAT(VerifyIngestionVerificationPayload(payload),
              StatusIs(absl::StatusCode::kFailedPrecondition, "ThresholdReport run_id mismatch."));
}

TEST(VerifyIngestionVerificationPayloadTest, FailsWhenApprovedPartitionProjectIdZeroOrNegative) {
  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  for (int32_t invalid_project_id : {0, -1}) {
    const PartitionSpec invalid_approved[] = {
        {.project_id = invalid_project_id, .metric_id = 2, .epoch_index = 100}};
    const IngestionVerificationPayload payload =
        CreateVerificationPayload(signed_context, "run-100", invalid_approved);

    EXPECT_THAT(VerifyIngestionVerificationPayload(payload),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         "Invalid MetricEpochKey in approved_partitions."));
  }
}

TEST(VerifyIngestionVerificationPayloadTest, FailsWhenApprovedPartitionMetricIdZeroOrNegative) {
  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  for (int32_t invalid_metric_id : {0, -1}) {
    const PartitionSpec invalid_approved[] = {
        {.project_id = 1, .metric_id = invalid_metric_id, .epoch_index = 100}};
    const IngestionVerificationPayload payload =
        CreateVerificationPayload(signed_context, "run-100", invalid_approved);

    EXPECT_THAT(VerifyIngestionVerificationPayload(payload),
                StatusIs(absl::StatusCode::kInvalidArgument,
                         "Invalid MetricEpochKey in approved_partitions."));
  }
}

TEST(VerifyIngestionVerificationPayloadTest, FailsWhenApprovedPartitionEpochIndexNegative) {
  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec invalid_approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = -1}};
  const IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-100", invalid_approved);

  EXPECT_THAT(VerifyIngestionVerificationPayload(payload),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "Invalid MetricEpochKey in approved_partitions."));
}

TEST(VerifyIngestionVerificationResultTest, SucceedsWithoutCanonicalFingerprint) {
  const SignedRunContext signed_context = CreateSignedRunContext("run-abc-123");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  IngestionVerificationResult result =
      CreateVerificationResult(signed_context, "run-abc-123", approved);
  result.clear_canonical_fingerprint();

  const absl::StatusOr<VerifiedIngestionVerificationResult> verified =
      VerifyIngestionVerificationResult(result);
  ASSERT_THAT(verified, IsOk());
  EXPECT_THAT(verified->approved_partitions, SizeIs(1));
  EXPECT_THAT(verified->canonical_fingerprint.entries(), IsEmpty());
}

TEST(VerifyIngestionVerificationResultTest, PopulatesCanonicalFingerprint) {
  const SignedRunContext signed_context = CreateSignedRunContext("run-abc-123");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  IngestionVerificationResult result =
      CreateVerificationResult(signed_context, "run-abc-123", approved);
  result.mutable_canonical_fingerprint()->set_run_id(PadRunId("run-abc-123"));
  result.mutable_canonical_fingerprint()->set_stage_id(STAGE_INGESTION);
  result.mutable_canonical_fingerprint()->set_is_partial(false);
  auto* entry = result.mutable_canonical_fingerprint()->add_entries();
  *entry->mutable_metric_epoch_key() = CreateMetricEpochKey(approved[0]);
  entry->set_message_count(42);

  const absl::StatusOr<VerifiedIngestionVerificationResult> verified =
      VerifyIngestionVerificationResult(result);
  ASSERT_THAT(verified, IsOk());
  ASSERT_THAT(verified->canonical_fingerprint.entries(), SizeIs(1));
  EXPECT_EQ(verified->canonical_fingerprint.entries(0).message_count(), 42);
}

TEST(VerifyIngestionVerificationResultTest, FailsWhenCanonicalFingerprintRunIdMismatch) {
  const SignedRunContext signed_context = CreateSignedRunContext("run-abc-123");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  IngestionVerificationResult result =
      CreateVerificationResult(signed_context, "run-abc-123", approved);
  result.mutable_canonical_fingerprint()->set_run_id(PadRunId("wrong-run-id"));

  EXPECT_THAT(VerifyIngestionVerificationResult(result),
              StatusIs(absl::StatusCode::kFailedPrecondition, "RunFingerprint run_id mismatch."));
}

TEST(VerifyIngestionVerificationResultTest, FailsWhenCanonicalFingerprintStageIdNotIngestion) {
  const SignedRunContext signed_context = CreateSignedRunContext("run-abc-123");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  IngestionVerificationResult result =
      CreateVerificationResult(signed_context, "run-abc-123", approved);
  result.mutable_canonical_fingerprint()->set_run_id(PadRunId("run-abc-123"));
  result.mutable_canonical_fingerprint()->set_stage_id(STAGE_RELEASE);

  EXPECT_THAT(VerifyIngestionVerificationResult(result),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "RunFingerprint stage_id must be STAGE_INGESTION."));
}

TEST(VerifyIngestionVerificationResultTest, FailsWhenCanonicalFingerprintIsPartial) {
  const SignedRunContext signed_context = CreateSignedRunContext("run-abc-123");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  IngestionVerificationResult result =
      CreateVerificationResult(signed_context, "run-abc-123", approved);
  result.mutable_canonical_fingerprint()->set_run_id(PadRunId("run-abc-123"));
  result.mutable_canonical_fingerprint()->set_stage_id(STAGE_INGESTION);
  result.mutable_canonical_fingerprint()->set_is_partial(true);

  EXPECT_THAT(
      VerifyIngestionVerificationResult(result),
      StatusIs(absl::StatusCode::kInvalidArgument, "RunFingerprint is_partial must be false."));
}

TEST(VerifyIngestionVerificationResultTest, NeverLeaksSecretsInErrorMessages) {
  const std::string run_id = PadRunId("secret-run-id-123456");
  const SignedRunContext signed_context = CreateSignedRunContext("secret-run-id-123456");
  RunContext parsed_context;
  ASSERT_TRUE(parsed_context.ParseFromString(signed_context.serialized_run_context()));
  const std::string run_hash_key = parsed_context.run_hash_key();
  const std::string release_key = parsed_context.release_key();

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const IngestionVerificationResult mismatch_result =
      CreateVerificationResult(signed_context, "mismatched-run-id", approved);

  const absl::StatusOr<VerifiedIngestionVerificationResult> verified =
      VerifyIngestionVerificationResult(mismatch_result);
  ASSERT_FALSE(verified.ok());
  EXPECT_THAT(verified.status().message(), Not(HasSubstr(run_id)));
  EXPECT_THAT(verified.status().message(), Not(HasSubstr(run_hash_key)));
  EXPECT_THAT(verified.status().message(), Not(HasSubstr(release_key)));
}

TEST(VerifyIngestionVerificationPayloadTest, NeverLeaksSecretsInErrorMessages) {
  const std::string run_id = PadRunId("secret-run-id-123456");
  const SignedRunContext signed_context = CreateSignedRunContext("secret-run-id-123456");
  RunContext parsed_context;
  ASSERT_TRUE(parsed_context.ParseFromString(signed_context.serialized_run_context()));
  const std::string run_hash_key = parsed_context.run_hash_key();
  const std::string release_key = parsed_context.release_key();

  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  const IngestionVerificationPayload mismatch_payload =
      CreateVerificationPayload(signed_context, "mismatched-run-id", approved);

  const absl::StatusOr<VerifiedIngestionVerificationResult> verified =
      VerifyIngestionVerificationPayload(mismatch_payload);
  ASSERT_FALSE(verified.ok());
  EXPECT_THAT(verified.status().message(), Not(HasSubstr(run_id)));
  EXPECT_THAT(verified.status().message(), Not(HasSubstr(run_hash_key)));
  EXPECT_THAT(verified.status().message(), Not(HasSubstr(release_key)));
}

TEST(VerifyIngestionVerificationPayloadTest, FailsWhenMissingCanonicalFingerprint) {
  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-100", approved);
  payload.mutable_success()->clear_canonical_fingerprint();

  EXPECT_THAT(VerifyIngestionVerificationPayload(payload),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "IngestionVerificationResult missing canonical_fingerprint."));
}

TEST(VerifyIngestionVerificationPayloadTest, FailsWhenCanonicalFingerprintRunIdMismatch) {
  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-100", approved);
  payload.mutable_success()->mutable_canonical_fingerprint()->set_run_id(
      PadRunId("different-run-999"));

  EXPECT_THAT(VerifyIngestionVerificationPayload(payload),
              StatusIs(absl::StatusCode::kFailedPrecondition, "RunFingerprint run_id mismatch."));
}

TEST(VerifyIngestionVerificationPayloadTest, FailsWhenCanonicalFingerprintStageIdNotIngestion) {
  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-100", approved);
  payload.mutable_success()->mutable_canonical_fingerprint()->set_stage_id(STAGE_RELEASE);

  EXPECT_THAT(VerifyIngestionVerificationPayload(payload),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       "RunFingerprint stage_id must be STAGE_INGESTION."));
}

TEST(VerifyIngestionVerificationPayloadTest, FailsWhenCanonicalFingerprintIsPartial) {
  const SignedRunContext signed_context = CreateSignedRunContext("run-100");
  const PartitionSpec approved[] = {{.project_id = 1, .metric_id = 2, .epoch_index = 100}};
  IngestionVerificationPayload payload =
      CreateVerificationPayload(signed_context, "run-100", approved);
  payload.mutable_success()->mutable_canonical_fingerprint()->set_is_partial(true);

  EXPECT_THAT(
      VerifyIngestionVerificationPayload(payload),
      StatusIs(absl::StatusCode::kInvalidArgument, "RunFingerprint is_partial must be false."));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
