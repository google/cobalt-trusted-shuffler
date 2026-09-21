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

#include "src/containers/ingest_uploads/ingest_uploads_fn.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/cord.h"
#include "fcp/confidentialcompute/client_payload.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/containers/base/counter_names.h"
#include "src/crypto/verify_signed_run_context.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/proto/trusted_shuffler_upload.pb.h"
#include "src/sketch/hll_sketch.h"
#include "src/testing/fake_context.h"
#include "src/testing/test_utils.h"
#include "tensorflow_federated/cc/core/impl/aggregation/core/mutable_vector_data.h"
#include "tensorflow_federated/cc/core/impl/aggregation/core/tensor.h"
#include "tensorflow_federated/cc/core/impl/aggregation/protocol/checkpoint_builder.h"
#include "tensorflow_federated/cc/core/impl/aggregation/protocol/federated_compute_checkpoint_builder.h"

namespace cobalt::trusted_shuffler {
namespace {

using absl_testing::IsOk;
using absl_testing::StatusIs;
using ::cobalt::trusted_shuffler::testing::FakeContext;
using ::cobalt::trusted_shuffler::testing::FakeDoContext;
using ::cobalt::trusted_shuffler::testing::FakeFnContext;
using testing::CreateTestFcCheckpoint;
using testing::CreateValidSignedRunContext;
using ::testing::Eq;
using ::testing::SizeIs;

constexpr uint32_t kMessageKeyIndex = 0;
constexpr uint32_t kReportKeyIndex = 1;
const std::string kTestRunId(kRunIdByteLength, 'a');
constexpr absl::string_view kTestPrivacyId = "test_privacy_id";

IngestUploadsFn::KV CreateBlobRecord(absl::string_view checkpoint_data) {
  return IngestUploadsFn::KV(std::string(checkpoint_data));
}

TEST(IngestUploadsFnTest, IngestsConfigOnRecordZeroAndProcessesSubsequentUploads) {
  FakeContext context;
  FakeDoContext do_context(context);
  FakeFnContext fn_context(context);
  IngestUploadsFn fn;

  EXPECT_FALSE(fn.HasConfig());

  SignedRunContext signed_run_context = CreateValidSignedRunContext(
      /*project_id=*/1, /*metric_id=*/100, kTestRunId, /*min_epoch_index=*/0,
      /*max_epoch_index=*/10);

  // Record 0: Ingests SignedRunContext configuration
  ASSERT_THAT(fn.Do(IngestUploadsFn::KV(signed_run_context.SerializeAsString()), do_context),
              IsOk());

  EXPECT_TRUE(fn.HasConfig());
  ASSERT_TRUE(fn.Config().has_value());
  EXPECT_THAT(fn.GetRunContext().run_id(), Eq(kTestRunId));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigCount)), Eq(1));

  // Record 1: Valid message upload checkpoint
  TrustedShufflerUpload upload;
  upload.set_project_id(1);
  upload.set_metric_id(100);
  upload.set_serialized_payload("sample_payload_data");

  std::string checkpoint_data =
      CreateTestFcCheckpoint({upload}, {"1970-01-01T00:00:00Z"}, std::string(kTestPrivacyId));
  ASSERT_THAT(fn.Do(CreateBlobRecord(checkpoint_data), do_context), IsOk());
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kEncryptedOutputRecordCount)), Eq(1));

  // Verify message emitted encrypted with key index 0 (reencryption_key 2)
  auto encrypted_messages = context.Encrypted();
  ASSERT_THAT(encrypted_messages, SizeIs(1));
  const auto emitted = encrypted_messages[0];
  EXPECT_THAT(emitted.first, Eq(kMessageKeyIndex));

  IngestMessagesHeader header;
  ASSERT_TRUE(emitted.second.key.UnpackTo(&header));
  EXPECT_TRUE(header.has_metric_epoch_key());
  EXPECT_THAT(header.metric_epoch_key().project_id(), Eq(1));
  EXPECT_THAT(header.metric_epoch_key().metric_id(), Eq(100));
  EXPECT_THAT(header.metric_epoch_key().epoch_index(), Eq(0));

  ShuffledMessage shuffled_msg;
  ASSERT_TRUE(shuffled_msg.ParseFromString(emitted.second.data));
  EXPECT_THAT(shuffled_msg.customer_message(), Eq("sample_payload_data"));

  // Finalize replica
  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), fn_context), IsOk());
  EXPECT_THAT(context.GetCounters().at(std::string(kEncryptedOutputRecordCount)), Eq(2));

  // Verify partial report emitted encrypted with key index 1 (reencryption_key 3)
  auto all_encrypted = context.Encrypted();
  ASSERT_THAT(all_encrypted, SizeIs(2));
  const auto report_emitted = all_encrypted[1];
  EXPECT_THAT(report_emitted.first, Eq(kReportKeyIndex));

  IngestMessagesHeader report_header;
  ASSERT_TRUE(report_emitted.second.key.UnpackTo(&report_header));
  EXPECT_TRUE(report_header.has_partial_ingestion_report());

  PartialIngestionReport report;
  ASSERT_TRUE(report.ParseFromString(report_emitted.second.data));
  EXPECT_THAT(report.partial_fingerprint().stage_id(), Eq(STAGE_INGESTION));
  EXPECT_TRUE(report.partial_fingerprint().is_partial());
  ASSERT_THAT(report.metric_epoch_sketches(), SizeIs(1));
  EXPECT_THAT(report.metric_epoch_sketches(0).metric_epoch_key().metric_id(), Eq(100));
  EXPECT_FALSE(report.metric_epoch_sketches(0).hyperloglog_sketch().registers().empty());
}

TEST(IngestUploadsFnTest, ProcessesEncodedClientPayloadInput) {
  FakeContext context;
  FakeDoContext do_context(context);
  IngestUploadsFn fn;

  SignedRunContext signed_run_context = CreateValidSignedRunContext(
      /*project_id=*/1, /*metric_id=*/100, kTestRunId, /*min_epoch_index=*/0,
      /*max_epoch_index=*/10);
  ASSERT_THAT(fn.Do(IngestUploadsFn::KV(signed_run_context.SerializeAsString()), do_context),
              IsOk());

  TrustedShufflerUpload upload;
  upload.set_project_id(1);
  upload.set_metric_id(100);
  upload.set_serialized_payload("encoded_client_payload_data");

  std::string checkpoint_data =
      CreateTestFcCheckpoint({upload}, {"1970-01-01T00:00:00Z"}, std::string(kTestPrivacyId));

  fcp::confidential_compute::ClientPayloadHeader header;
  header.encrypted_symmetric_key = "test_sym_key";
  header.encapsulated_public_key = "test_pub_key";
  header.serialized_blob_header = "test_blob_header";

  std::string encoded_input =
      fcp::confidential_compute::EncodeClientPayload(header, checkpoint_data);

  ASSERT_THAT(fn.Do(CreateBlobRecord(encoded_input), do_context), IsOk());

  auto encrypted_messages = context.Encrypted();
  ASSERT_THAT(encrypted_messages, SizeIs(1));

  ShuffledMessage shuffled_msg;
  ASSERT_TRUE(shuffled_msg.ParseFromString(encrypted_messages[0].second.data));
  EXPECT_THAT(shuffled_msg.customer_message(), Eq("encoded_client_payload_data"));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordCount)), Eq(1));
}

TEST(IngestUploadsFnTest, DropsUnallowedMetrics) {
  FakeContext context;
  FakeDoContext do_context(context);
  FakeFnContext fn_context(context);
  IngestUploadsFn fn;

  SignedRunContext signed_run_context = CreateValidSignedRunContext(
      /*project_id=*/1, /*metric_id=*/100, kTestRunId, /*min_epoch_index=*/0,
      /*max_epoch_index=*/10);
  ASSERT_THAT(fn.Do(IngestUploadsFn::KV(signed_run_context.SerializeAsString()), do_context),
              IsOk());

  // Unallowed metric
  TrustedShufflerUpload upload;
  upload.set_project_id(2);
  upload.set_metric_id(999);
  upload.set_serialized_payload("invalid_metric_payload");

  std::string checkpoint_data =
      CreateTestFcCheckpoint({upload}, {"1970-01-01T00:00:00Z"}, std::string(kTestPrivacyId));
  ASSERT_THAT(fn.Do(CreateBlobRecord(checkpoint_data), do_context), IsOk());

  // Nothing emitted for dropped message
  EXPECT_THAT(context.Encrypted(), SizeIs(0));

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), fn_context), IsOk());
  auto all_encrypted = context.Encrypted();
  ASSERT_THAT(all_encrypted, SizeIs(1));

  PartialIngestionReport report;
  ASSERT_TRUE(report.ParseFromString(all_encrypted[0].second.data));
  EXPECT_THAT(report.metric_epoch_sketches(), SizeIs(0));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordDropped)), Eq(1));
}

TEST(IngestUploadsFnTest, DropsInvalidCheckpointAndEmitsError) {
  FakeContext context;
  FakeDoContext do_context(context);
  IngestUploadsFn fn;

  SignedRunContext signed_run_context = CreateValidSignedRunContext(
      /*project_id=*/1, /*metric_id=*/100, kTestRunId, /*min_epoch_index=*/0,
      /*max_epoch_index=*/10);
  ASSERT_THAT(fn.Do(IngestUploadsFn::KV(signed_run_context.SerializeAsString()), do_context),
              IsOk());

  TrustedShufflerUpload upload;
  upload.set_project_id(1);
  upload.set_metric_id(100);
  upload.set_serialized_payload("sample_payload_data");

  // Checkpoint without event times is invalid.
  std::string checkpoint_data =
      CreateTestFcCheckpoint({upload}, /*event_times=*/{}, std::string(kTestPrivacyId));
  ASSERT_THAT(fn.Do(CreateBlobRecord(checkpoint_data), do_context), IsOk());

  // Checkpoint is dropped.
  EXPECT_THAT(context.Encrypted(), SizeIs(0));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordDropped)), Eq(1));
}

TEST(IngestUploadsFnTest, FailsOnMissingRunConfig) {
  testing::FakeContext context;
  testing::FakeDoContext do_context(context);
  IngestUploadsFn fn;

  RunContext run_context;
  run_context.set_run_id(std::string(kTestRunId));
  run_context.set_run_hash_key(std::string(kRunHashKeyByteLength, 'b'));
  // No run_config set.
  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature("test_signature");

  EXPECT_THAT(fn.Do(IngestUploadsFn::KV(signed_context.SerializeAsString()), do_context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigInvalid)), Eq(1));
}

TEST(IngestUploadsFnTest, FailsOnEmptyMetricIdentifiers) {
  testing::FakeContext context;
  testing::FakeDoContext do_context(context);
  IngestUploadsFn fn;

  RunContext run_context;
  run_context.set_run_id(std::string(kTestRunId));
  run_context.set_run_hash_key(std::string(kRunHashKeyByteLength, 'b'));
  auto* run_config = run_context.mutable_run_config();
  run_config->set_min_epoch_index(0);
  run_config->set_max_epoch_index(10);
  run_config->set_target_batch_size(100);
  // No metric identifiers configured.
  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature("test_signature");

  EXPECT_THAT(fn.Do(IngestUploadsFn::KV(signed_context.SerializeAsString()), do_context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigInvalid)), Eq(1));
}

TEST(IngestUploadsFnTest, FailsOnEmptySignature) {
  testing::FakeContext context;
  testing::FakeDoContext do_context(context);
  IngestUploadsFn fn;

  SignedRunContext signed_context = CreateValidSignedRunContext(
      /*project_id=*/1, /*metric_id=*/100, kTestRunId, /*min_epoch_index=*/0,
      /*max_epoch_index=*/10);
  signed_context.clear_signature();

  EXPECT_THAT(fn.Do(IngestUploadsFn::KV(signed_context.SerializeAsString()), do_context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputConfigInvalid)), Eq(1));
}

TEST(IngestUploadsFnTest, AllowsOnlyEpochZeroWhenMinAndMaxEpochAreZero) {
  testing::FakeContext context;
  testing::FakeDoContext do_context(context);
  IngestUploadsFn fn;

  SignedRunContext signed_context = CreateValidSignedRunContext(
      /*project_id=*/1, /*metric_id=*/100, kTestRunId, /*min_epoch_index=*/0,
      /*max_epoch_index=*/0);
  ASSERT_THAT(fn.Do(IngestUploadsFn::KV(signed_context.SerializeAsString()), do_context), IsOk());

  TrustedShufflerUpload upload;
  upload.set_project_id(1);
  upload.set_metric_id(100);
  upload.set_serialized_payload("sample_payload");

  // Epoch 0 (1970-01-01) is allowed.
  std::string checkpoint_epoch_0 =
      CreateTestFcCheckpoint({upload}, {"1970-01-01T00:00:00Z"}, std::string(kTestPrivacyId));
  ASSERT_THAT(fn.Do(CreateBlobRecord(checkpoint_epoch_0), do_context), IsOk());
  EXPECT_THAT(context.Encrypted(), SizeIs(1));

  // Epoch 1 (1970-01-02) is not allowed.
  std::string checkpoint_epoch_1 =
      CreateTestFcCheckpoint({upload}, {"1970-01-02T00:00:00Z"}, std::string(kTestPrivacyId));
  ASSERT_THAT(fn.Do(CreateBlobRecord(checkpoint_epoch_1), do_context), IsOk());
  EXPECT_THAT(context.Encrypted(), SizeIs(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordDropped)), Eq(1));
}

TEST(IngestUploadsFnTest, FailsOnEmptyFirstRecordAndRejectsSubsequentRecords) {
  testing::FakeContext context;
  testing::FakeDoContext do_context(context);
  IngestUploadsFn fn;

  EXPECT_THAT(fn.Do(IngestUploadsFn::KV(""), do_context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());

  // One-shot semantics: subsequent records are rejected.
  EXPECT_THAT(fn.Do(CreateBlobRecord("checkpoint_data"), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(IngestUploadsFnTest, EstimatesDeviceCardinalityUsingPrivacyId) {
  testing::FakeContext context;
  testing::FakeDoContext do_context(context);
  testing::FakeFnContext fn_context(context);
  IngestUploadsFn fn;

  const SignedRunContext signed_run_context = CreateValidSignedRunContext(
      /*project_id=*/1, /*metric_id=*/100, kTestRunId, /*min_epoch_index=*/0,
      /*max_epoch_index=*/10);
  ASSERT_THAT(fn.Do(IngestUploadsFn::KV(signed_run_context.SerializeAsString()), do_context),
              IsOk());

  TrustedShufflerUpload upload1;
  upload1.set_project_id(1);
  upload1.set_metric_id(100);
  upload1.set_serialized_payload("payload_1");

  TrustedShufflerUpload upload2;
  upload2.set_project_id(1);
  upload2.set_metric_id(100);
  upload2.set_serialized_payload("payload_2");

  TrustedShufflerUpload upload3;
  upload3.set_project_id(1);
  upload3.set_metric_id(100);
  upload3.set_serialized_payload("payload_3");

  // Two checkpoints from "device_1" with different payloads and timestamps in the same epoch.
  const std::string checkpoint1 =
      CreateTestFcCheckpoint({upload1}, {"1970-01-01T00:00:01Z"}, std::string("device_1"));
  const std::string checkpoint2 =
      CreateTestFcCheckpoint({upload2}, {"1970-01-01T00:00:02Z"}, std::string("device_1"));
  // One checkpoint from "device_2" in the same epoch.
  const std::string checkpoint3 =
      CreateTestFcCheckpoint({upload3}, {"1970-01-01T00:00:03Z"}, std::string("device_2"));

  ASSERT_THAT(fn.Do(CreateBlobRecord(checkpoint1), do_context), IsOk());
  ASSERT_THAT(fn.Do(CreateBlobRecord(checkpoint2), do_context), IsOk());
  ASSERT_THAT(fn.Do(CreateBlobRecord(checkpoint3), do_context), IsOk());

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), fn_context), IsOk());

  const std::vector<std::pair<int, IngestUploadsFn::KV>> all_encrypted = context.Encrypted();
  // 3 messages + 1 report
  ASSERT_THAT(all_encrypted, SizeIs(4));
  const std::pair<int, IngestUploadsFn::KV>& report_emitted = all_encrypted[3];
  EXPECT_THAT(report_emitted.first, Eq(kReportKeyIndex));

  PartialIngestionReport report;
  ASSERT_TRUE(report.ParseFromString(report_emitted.second.data));
  ASSERT_THAT(report.metric_epoch_sketches(), SizeIs(1));

  const absl::StatusOr<HllSketch> sketch =
      HllSketch::FromProto(report.metric_epoch_sketches(0).hyperloglog_sketch());
  ASSERT_THAT(sketch, IsOk());
  EXPECT_THAT(sketch->EstimateCardinality(), Eq(2));
}

TEST(IngestUploadsFnTest, DropsCheckpointMissingPrivacyId) {
  testing::FakeContext context;
  testing::FakeDoContext do_context(context);
  IngestUploadsFn fn;

  const SignedRunContext signed_run_context = CreateValidSignedRunContext(
      /*project_id=*/1, /*metric_id=*/100, kTestRunId, /*min_epoch_index=*/0,
      /*max_epoch_index=*/10);
  ASSERT_THAT(fn.Do(IngestUploadsFn::KV(signed_run_context.SerializeAsString()), do_context),
              IsOk());

  TrustedShufflerUpload upload;
  upload.set_project_id(1);
  upload.set_metric_id(100);
  upload.set_serialized_payload("sample_payload_data");

  const std::string checkpoint_data =
      CreateTestFcCheckpoint({upload}, {"1970-01-01T00:00:00Z"}, /*privacy_id=*/std::nullopt);
  ASSERT_THAT(fn.Do(CreateBlobRecord(checkpoint_data), do_context), IsOk());

  EXPECT_THAT(context.Encrypted(), SizeIs(0));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordDropped)), Eq(1));
}

TEST(IngestUploadsFnTest, FailsOnCorruptedConfigProtoAndRejectsSubsequentRecords) {
  testing::FakeContext context;
  testing::FakeDoContext do_context(context);
  IngestUploadsFn fn;

  // Invalid proto wire data
  EXPECT_THAT(fn.Do(IngestUploadsFn::KV("\xff\xff\xff\xff"), do_context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());

  // One-shot semantics: subsequent records are rejected.
  EXPECT_THAT(fn.Do(CreateBlobRecord("checkpoint_data"), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(IngestUploadsFnTest, DropsCorruptedRecordPayload) {
  testing::FakeContext context;
  testing::FakeDoContext do_context(context);
  IngestUploadsFn fn;

  const SignedRunContext signed_run_context = CreateValidSignedRunContext(
      /*project_id=*/1, /*metric_id=*/100, kTestRunId, /*min_epoch_index=*/0,
      /*max_epoch_index=*/10);
  ASSERT_THAT(fn.Do(IngestUploadsFn::KV(signed_run_context.SerializeAsString()), do_context),
              IsOk());

  // Corrupted checkpoint payload is dropped and counted via kInputRecordDropped.
  EXPECT_THAT(fn.Do(IngestUploadsFn::KV("corrupted_record_payload"), do_context), IsOk());
  EXPECT_THAT(context.Encrypted(), SizeIs(0));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordDropped)), Eq(1));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
