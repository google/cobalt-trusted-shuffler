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

#include <cstdint>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "cc/crypto/signing_key.h"
#include "containers/session.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/proto/common.pb.h"
#include "src/proto/config_constraints.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/proto/trusted_shuffler_upload.pb.h"
#include "src/testing/integration/encrypt_output_router.h"
#include "src/testing/integration/ingest_uploads_router.h"
#include "src/testing/integration/ingestion_verification_router.h"
#include "src/testing/integration/pipeline_runner.h"
#include "src/testing/integration/records.h"
#include "src/testing/integration/threshold_filter_router.h"
#include "src/testing/test_utils.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler::testing::integration {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;

// Returns the customer payloads ScatterShuffle emitted, checking each message
// records the one shuffle pass it has been through.
std::vector<std::string> ScatteredPayloads(absl::Span<const EncryptedKVBlob> blobs) {
  std::vector<std::string> payloads;
  for (const EncryptedKVBlob& blob : blobs) {
    ShuffledMessage message;
    EXPECT_TRUE(message.ParseFromString(blob.ciphertext()));
    EXPECT_EQ(message.shuffle_count(), 1);
    payloads.push_back(message.customer_message());
  }
  return payloads;
}

// Returns the customer payloads GatherShuffle emitted, checking each message
// records both shuffle passes.
std::vector<std::string> GatheredPayloads(absl::Span<const EncryptedBlob> blobs) {
  std::vector<std::string> payloads;
  for (const EncryptedBlob& blob : blobs) {
    ShuffledMessage message;
    EXPECT_TRUE(message.ParseFromString(blob.ciphertext()));
    EXPECT_EQ(message.shuffle_count(), 2);
    payloads.push_back(message.customer_message());
  }
  return payloads;
}

// Deterministic signing key implementation for testing.
class TestSigningKey : public oak::crypto::SigningKeyHandle {
 public:
  TestSigningKey() : key_(&cobalt::trusted_shuffler::testing::GetDefaultTestSigningKey()) {}
  explicit TestSigningKey(const cobalt::trusted_shuffler::testing::TestSigningKey* key)
      : key_(key) {}

  absl::StatusOr<oak::crypto::v1::Signature> Sign(absl::string_view message) override {
    oak::crypto::v1::Signature sig;
    sig.set_signature(key_->Sign(message));
    return sig;
  }

 private:
  const cobalt::trusted_shuffler::testing::TestSigningKey* key_;
};

struct UploadSpec {
  int32_t project_id;
  int32_t metric_id;
  std::string payload;
  std::string event_time;
  std::string privacy_id;
};

struct MetricThresholdSpec {
  int32_t project_id;
  int32_t metric_id;
  int32_t threshold;
};

std::string CreateCheckpointFromSpec(const UploadSpec& spec) {
  TrustedShufflerUpload upload;
  upload.set_project_id(spec.project_id);
  upload.set_metric_id(spec.metric_id);
  upload.set_serialized_payload(spec.payload);
  return CreateTestFcCheckpoint({upload}, {spec.event_time}, spec.privacy_id);
}

RunConfig CreateTestRunConfig(absl::Span<const MetricIdentifierSpec> metrics,
                              const int64_t min_epoch, const int64_t max_epoch,
                              const int32_t batch_size) {
  RunConfig config;
  for (const MetricIdentifierSpec& spec : metrics) {
    RunConfig::MetricIdentifier* const id = config.add_metric_identifiers();
    id->set_project_id(spec.project_id);
    id->set_metric_id(spec.metric_id);
  }
  config.set_min_epoch_index(min_epoch);
  config.set_max_epoch_index(max_epoch);
  config.set_target_batch_size(batch_size);
  return config;
}

TrustedShufflerConfigConstraints CreateTestConstraints(
    absl::Span<const MetricThresholdSpec> specs) {
  TrustedShufflerConfigConstraints constraints;
  for (const MetricThresholdSpec& spec : specs) {
    MetricThreshold* const mt = constraints.add_metric_thresholds();
    mt->set_project_id(spec.project_id);
    mt->set_metric_id(spec.metric_id);
    mt->set_threshold(spec.threshold);
  }
  return constraints;
}

TEST(ShufflerFlowIntegrationTest, EndToEndSingleSessionFlowVerifiesIngestionAndFiltersThresholds) {
  const int32_t project_id = 1001;
  const int32_t metric_id_above_threshold = 2;
  const int32_t metric_id_below_threshold = 3;
  const int64_t min_epoch_index = 0;
  const int64_t max_epoch_index = 10;
  const int32_t target_batch_size = 2;
  const int32_t min_unique_devices_threshold = 3;

  TestSigningKey signing_key;
  const RunConfig run_config = CreateTestRunConfig(
      {
          {.project_id = project_id, .metric_id = metric_id_above_threshold},
          {.project_id = project_id, .metric_id = metric_id_below_threshold},
      },
      min_epoch_index, max_epoch_index, target_batch_size);

  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id,
       .metric_id = metric_id_above_threshold,
       .threshold = min_unique_devices_threshold},
      {.project_id = project_id,
       .metric_id = metric_id_below_threshold,
       .threshold = min_unique_devices_threshold},
  });

  // Create 3 uploads for metric_id_above_threshold (3 unique devices >= 3)
  // and 2 uploads for metric_id_below_threshold (2 unique devices < 3).
  std::vector<std::string> checkpoints;
  for (int i = 0; i < 3; ++i) {
    checkpoints.push_back(CreateCheckpointFromSpec({
        .project_id = project_id,
        .metric_id = metric_id_above_threshold,
        .payload = absl::StrCat("approved_payload_", i),
        .event_time = "1970-01-01T00:00:00Z",
        .privacy_id = absl::StrCat("device_approved_", i),
    }));
  }
  for (int i = 0; i < 2; ++i) {
    checkpoints.push_back(CreateCheckpointFromSpec({
        .project_id = project_id,
        .metric_id = metric_id_below_threshold,
        .payload = absl::StrCat("dropped_payload_", i),
        .event_time = "1970-01-01T00:00:00Z",
        .privacy_id = absl::StrCat("device_dropped_", i),
    }));
  }

  absl::StatusOr<PipelineExecutionResult> result =
      ExecutePipeline(PipelineRouters{}, signing_key, run_config, constraints, checkpoints);
  ASSERT_THAT(result.status(), IsOk());

  // 1. IngestUploads emitted 5 message records + 1 partial report = 6 encrypted records.
  EXPECT_THAT(result->ingest_uploads_output.encrypted, SizeIs(6));

  // 2. IngestionVerification approved only metric_id_above_threshold because
  // metric_id_below_threshold had only 2 unique devices (< threshold 3).
  ASSERT_THAT(result->ingestion_verification_output.encrypted, SizeIs(1));
  IngestionVerificationPayload iv_payload;
  ASSERT_TRUE(
      iv_payload.ParseFromString(result->ingestion_verification_output.encrypted[0].ciphertext()));
  ASSERT_TRUE(iv_payload.has_success());
  ASSERT_EQ(iv_payload.success().threshold_report().approved_partitions_size(), 1);
  EXPECT_EQ(iv_payload.success().threshold_report().approved_partitions(0).metric_id(),
            metric_id_above_threshold);

  // 3. ThresholdFilter kept the 3 messages for metric_id_above_threshold and dropped
  // metric_id_below_threshold.
  ASSERT_THAT(result->threshold_filter_output.encrypted, SizeIs(3));

  // 4. PrePadding emitted 1 padding message for the single approved partition
  // (count 3 with target_batch_size 2 -> batch_num = 2, remainder = 1, padding_count = 1).
  EXPECT_THAT(result->pre_padding_output.encrypted, SizeIs(1));

  // 5. ScatterShuffle emitted 2 batches of actual_batch_size (2) = 4 records.
  EXPECT_THAT(result->scatter_shuffle_output.encrypted, SizeIs(4));

  // 6. GatherShuffle emitted 2 gather groups of 2 = 4 records, holding the same
  // messages ScatterShuffle emitted and nothing else.
  EXPECT_THAT(result->gather_shuffle_output.encrypted, SizeIs(4));
  EXPECT_THAT(
      GatheredPayloads(result->gather_shuffle_output.encrypted),
      UnorderedElementsAreArray(ScatteredPayloads(result->scatter_shuffle_output.encrypted)));
}

TEST(ShufflerFlowIntegrationTest, EndToEndMultipleMetricEpochKeysApprovesAllEligiblePartitions) {
  const int32_t project_id = 1001;
  const int32_t metric_id_a = 2;
  const int32_t metric_id_b = 4;
  const int64_t min_epoch_index = 0;
  const int64_t max_epoch_index = 10;
  const int32_t target_batch_size = 2;
  const int32_t min_unique_devices_threshold = 2;

  TestSigningKey signing_key;
  const RunConfig run_config = CreateTestRunConfig(
      {
          {.project_id = project_id, .metric_id = metric_id_a},
          {.project_id = project_id, .metric_id = metric_id_b},
      },
      min_epoch_index, max_epoch_index, target_batch_size);

  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id,
       .metric_id = metric_id_a,
       .threshold = min_unique_devices_threshold},
      {.project_id = project_id,
       .metric_id = metric_id_b,
       .threshold = min_unique_devices_threshold},
  });

  // Create uploads across 3 distinct MetricEpochKeys:
  // 1. (Metric A, Epoch 0): 2 unique devices ("1970-01-01T00:00:00Z") -> approved
  // 2. (Metric A, Epoch 1): 3 unique devices ("1970-01-02T00:00:00Z") -> approved
  // 3. (Metric B, Epoch 0): 2 unique devices ("1970-01-01T00:00:00Z") -> approved
  std::vector<std::string> checkpoints;
  for (int i = 0; i < 2; ++i) {
    checkpoints.push_back(CreateCheckpointFromSpec({
        .project_id = project_id,
        .metric_id = metric_id_a,
        .payload = absl::StrCat("metricA_epoch0_", i),
        .event_time = "1970-01-01T00:00:00Z",
        .privacy_id = absl::StrCat("dev_A0_", i),
    }));
  }
  for (int i = 0; i < 3; ++i) {
    checkpoints.push_back(CreateCheckpointFromSpec({
        .project_id = project_id,
        .metric_id = metric_id_a,
        .payload = absl::StrCat("metricA_epoch1_", i),
        .event_time = "1970-01-02T00:00:00Z",
        .privacy_id = absl::StrCat("dev_A1_", i),
    }));
  }
  for (int i = 0; i < 2; ++i) {
    checkpoints.push_back(CreateCheckpointFromSpec({
        .project_id = project_id,
        .metric_id = metric_id_b,
        .payload = absl::StrCat("metricB_epoch0_", i),
        .event_time = "1970-01-01T00:00:00Z",
        .privacy_id = absl::StrCat("dev_B0_", i),
    }));
  }

  absl::StatusOr<PipelineExecutionResult> result =
      ExecutePipeline(PipelineRouters{}, signing_key, run_config, constraints, checkpoints);
  ASSERT_THAT(result.status(), IsOk());

  // IngestUploads: 7 messages + 1 partial report = 8 encrypted records.
  EXPECT_THAT(result->ingest_uploads_output.encrypted, SizeIs(8));

  // IngestionVerification: all 3 partitions approved.
  ASSERT_THAT(result->ingestion_verification_output.encrypted, SizeIs(1));
  IngestionVerificationPayload iv_payload;
  ASSERT_TRUE(
      iv_payload.ParseFromString(result->ingestion_verification_output.encrypted[0].ciphertext()));
  ASSERT_TRUE(iv_payload.has_success());
  EXPECT_EQ(iv_payload.success().threshold_report().approved_partitions_size(), 3);

  // ThresholdFilter: all 7 messages survive filtering.
  EXPECT_THAT(result->threshold_filter_output.encrypted, SizeIs(7));

  // PrePadding: with target_batch_size = 2, (Metric A, Epoch 1) has count 3 (batch_num = 2,
  // padding_count = 1), while (Metric A, Epoch 0) and (Metric B, Epoch 0) have count 2
  // (batch_num = 1, padding_count = 0).
  ASSERT_THAT(result->pre_padding_output.encrypted, SizeIs(1));
  absl::flat_hash_map<MetricEpochKey, int> padding_counts;
  for (const EncryptedKVBlob& blob : result->pre_padding_output.encrypted) {
    MetricEpochKey key;
    ASSERT_TRUE(blob.key().UnpackTo(&key));
    padding_counts[key]++;
  }
  EXPECT_THAT(padding_counts,
              UnorderedElementsAre(
                  Pair(CreateMetricEpochKey(
                           {.project_id = project_id, .metric_id = metric_id_a, .epoch_index = 1}),
                       1)));

  // ScatterShuffle: 7 real + 1 padding = 8 records total across 4 batches of 2.
  EXPECT_THAT(result->scatter_shuffle_output.encrypted, SizeIs(8));

  // GatherShuffle: 8 records total after second shuffle pass.
  EXPECT_THAT(result->gather_shuffle_output.encrypted, SizeIs(8));
}

TEST(ShufflerFlowIntegrationTest, PipelineProcessesCheckpointsAcrossMultipleIngestUploadsShards) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;

  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 5);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });

  std::vector<std::string> checkpoints;
  for (int i = 0; i < 4; ++i) {
    checkpoints.push_back(CreateCheckpointFromSpec({
        .project_id = project_id,
        .metric_id = metric_id,
        .payload = absl::StrCat("payload_", i),
        .event_time = "1970-01-01T00:00:00Z",
        .privacy_id = absl::StrCat("device_", i),
    }));
  }

  const PipelineRouters routers{
      .ingest_uploads_router = RouteToTwoShardIngestUploads,
  };

  absl::StatusOr<PipelineExecutionResult> result =
      ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints);
  ASSERT_THAT(result.status(), IsOk());

  // IngestUploads across 2 shards emitted 4 messages + 2 partial reports (1 per shard) = 6 records.
  EXPECT_THAT(result->ingest_uploads_output.encrypted, SizeIs(6));

  absl::StatusOr<IngestUploadsOutputs> split =
      SplitIngestUploadsOutputs(result->ingest_uploads_output);
  ASSERT_THAT(split.status(), IsOk());
  EXPECT_THAT(split->ingested_messages, SizeIs(4));
  EXPECT_THAT(split->partial_reports, SizeIs(2));

  // IngestionVerification processes both partial reports and approves the partition.
  ASSERT_THAT(result->ingestion_verification_output.encrypted, SizeIs(1));
  IngestionVerificationPayload iv_payload;
  ASSERT_TRUE(
      iv_payload.ParseFromString(result->ingestion_verification_output.encrypted[0].ciphertext()));
  ASSERT_TRUE(iv_payload.has_success());
  EXPECT_EQ(iv_payload.success().threshold_report().approved_partitions_size(), 1);

  // ThresholdFilter processes the 4 ingested messages.
  EXPECT_THAT(result->threshold_filter_output.encrypted, SizeIs(4));

  // PrePadding emits 0 padding messages for the approved partition (count 4 with target 5 ->
  // batch_num = 1, remainder = 0, actual_batch_size = 4).
  EXPECT_THAT(result->pre_padding_output.encrypted, IsEmpty());

  // ScatterShuffle emits actual_batch_size (4) records for the single approved partition.
  EXPECT_THAT(result->scatter_shuffle_output.encrypted, SizeIs(4));

  // GatherShuffle emits 4 records for the single gather group.
  EXPECT_THAT(result->gather_shuffle_output.encrypted, SizeIs(4));
}

TEST(ShufflerFlowIntegrationTest, PipelineFailsWhenSignedRunContextNotFirstInput) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;

  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 5);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });

  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
  };

  // Fault-injection router that places client upload checkpoints before the SignedRunContext.
  const PipelineRouters routers{
      .ingest_uploads_router = RouteToIngestUploadsWithLateRunContext,
  };

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ShufflerFlowIntegrationTest,
     PipelineFailsWhenSignedRunContextNotFirstInputToIngestionVerification) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;

  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 5);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });

  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
  };

  // Fault-injection router that places partial ingestion reports before the SignedRunContext.
  const PipelineRouters routers{
      .ingestion_verification_router = RouteToIngestionVerificationWithLateRunContext,
  };

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ShufflerFlowIntegrationTest, PipelineFailsWhenSecondShardAbortsInMultiShardStage) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;

  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 5);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });

  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_1",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_1",
      }),
  };

  // Shard 0 succeeds and emits outputs, but Shard 1 aborts on Record 0.
  // The stage stops execution and the overall pipeline fails.
  const PipelineRouters routers{
      .ingest_uploads_router = RouteToTwoShardIngestUploadsWithSecondShardAbort,
  };

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ShufflerFlowIntegrationTest,
     PipelineDropsInvalidCheckpointsAndProcessesRemainingValidUploads) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;
  const int64_t min_epoch_index = 0;
  const int64_t max_epoch_index = 5;
  const int32_t target_batch_size = 3;

  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, min_epoch_index,
                          max_epoch_index, target_batch_size);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 2},
  });

  const std::vector<std::string> checkpoints = {
      // 1. Malformed checkpoint bytes (fails ParseUploads)
      "malformed_checkpoint_bytes_not_a_valid_fc_checkpoint",
      // 2. Missing privacy_id
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "missing_privacy_id_payload",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "",
      }),
      // 3. Out-of-bounds epoch (day 20 > max_epoch_index 5)
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "out_of_bounds_epoch_payload",
          .event_time = "1970-01-21T00:00:00Z",
          .privacy_id = "device_out_of_bounds",
      }),
      // 4. Unregistered metric ID (metric 999 not in RunConfig)
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = 999,
          .payload = "unallowed_metric_payload",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_unallowed",
      }),
      // 5 & 6. Two valid checkpoints meeting threshold 2
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "valid_payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "valid_device_0",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "valid_payload_1",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "valid_device_1",
      }),
  };

  absl::StatusOr<PipelineExecutionResult> result =
      ExecutePipeline(PipelineRouters{}, signing_key, run_config, constraints, checkpoints);
  ASSERT_THAT(result.status(), IsOk());

  // Only the 2 valid messages + 1 partial report should be emitted by IngestUploads.
  EXPECT_THAT(result->ingest_uploads_output.encrypted, SizeIs(3));
  EXPECT_THAT(result->threshold_filter_output.encrypted, SizeIs(2));

  // ScatterShuffle emits actual_batch_size (2) records for the single approved partition.
  EXPECT_THAT(result->scatter_shuffle_output.encrypted, SizeIs(2));

  // GatherShuffle emits 2 records.
  EXPECT_THAT(result->gather_shuffle_output.encrypted, SizeIs(2));
}

TEST(ShufflerFlowIntegrationTest, ThresholdFilterFailsWhenVerificationPayloadNotFirstInput) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;
  const int64_t min_epoch_index = 0;
  const int64_t max_epoch_index = 10;
  const int32_t target_batch_size = 5;

  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, min_epoch_index,
                          max_epoch_index, target_batch_size);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });

  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
  };

  // Fault-injection router that places ingested messages before the IngestionVerificationPayload.
  const PipelineRouters routers{
      .threshold_filter_router = RouteToThresholdFilterWithLateVerificationPayload,
  };

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ShufflerFlowIntegrationTest, OrchestratorDropsPartialReportCausesThresholdRejection) {
  const int32_t project_id = 1001;
  // Metric A requires 4 unique devices across both shards (1 in Shard 0 + 3 in Shard 1).
  const int32_t metric_id_a = 2;
  // Metric B requires 2 unique devices, all present in Shard 0.
  const int32_t metric_id_b = 3;
  const int64_t min_epoch_index = 0;
  const int64_t max_epoch_index = 10;
  const int32_t target_batch_size = 5;

  TestSigningKey signing_key;
  const RunConfig run_config = CreateTestRunConfig(
      {
          {.project_id = project_id, .metric_id = metric_id_a},
          {.project_id = project_id, .metric_id = metric_id_b},
      },
      min_epoch_index, max_epoch_index, target_batch_size);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id_a, .threshold = 4},
      {.project_id = project_id, .metric_id = metric_id_b, .threshold = 2},
  });

  // Shard 0 receives the first 3 checkpoints (1 for Metric A, 2 for Metric B).
  // Shard 1 receives the last 3 checkpoints (3 for Metric A).
  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id_a,
          .payload = "payload_a_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_a_0",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id_b,
          .payload = "payload_b_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_b_0",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id_b,
          .payload = "payload_b_1",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_b_1",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id_a,
          .payload = "payload_a_1",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_a_1",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id_a,
          .payload = "payload_a_2",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_a_2",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id_a,
          .payload = "payload_a_3",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_a_3",
      }),
  };

  // First verify that with both shards' partial reports intact, both Metric A (1 + 3 = 4 >= 4)
  // and Metric B (2 >= 2) meet threshold, and all 6 messages pass ThresholdFilter.
  const PipelineRouters baseline_routers{
      .ingest_uploads_router = RouteToTwoShardIngestUploads,
  };
  absl::StatusOr<PipelineExecutionResult> baseline_result =
      ExecutePipeline(baseline_routers, signing_key, run_config, constraints, checkpoints);
  ASSERT_THAT(baseline_result.status(), IsOk());
  ASSERT_THAT(baseline_result->ingestion_verification_output.encrypted, SizeIs(1));
  IngestionVerificationPayload baseline_iv;
  ASSERT_TRUE(baseline_iv.ParseFromString(
      baseline_result->ingestion_verification_output.encrypted[0].ciphertext()));
  ASSERT_TRUE(baseline_iv.has_success());
  EXPECT_EQ(baseline_iv.success().threshold_report().approved_partitions_size(), 2);
  EXPECT_THAT(baseline_result->threshold_filter_output.encrypted, SizeIs(6));
  // Baseline PrePadding emitted 0 padding messages (both counts 4 and 2 <= target_batch_size 5,
  // so batch_num = 1 and remainder = 0).
  EXPECT_THAT(baseline_result->pre_padding_output.encrypted, IsEmpty());
  // Baseline ScatterShuffle emitted 6 records (4 for Metric A + 2 for Metric B).
  EXPECT_THAT(baseline_result->scatter_shuffle_output.encrypted, SizeIs(6));
  // Baseline GatherShuffle emitted 6 records.
  EXPECT_THAT(baseline_result->gather_shuffle_output.encrypted, SizeIs(6));

  // Now drop Shard 1's partial report. Metric B (2 devices in Shard 0) still meets threshold 2,
  // whereas Metric A drops from 4 unique devices to 1 (below threshold 4) and is rejected.
  // ThresholdFilter drops all 4 Metric A messages and emits only the 2 Metric B messages.
  const PipelineRouters fault_routers{
      .ingest_uploads_router = RouteToTwoShardIngestUploads,
      .ingest_uploads_output_splitter = SplitIngestUploadsOutputsWithDroppedReports,
  };
  absl::StatusOr<PipelineExecutionResult> result =
      ExecutePipeline(fault_routers, signing_key, run_config, constraints, checkpoints);
  ASSERT_THAT(result.status(), IsOk());

  ASSERT_THAT(result->ingestion_verification_output.encrypted, SizeIs(1));
  IngestionVerificationPayload iv_payload;
  ASSERT_TRUE(
      iv_payload.ParseFromString(result->ingestion_verification_output.encrypted[0].ciphertext()));
  ASSERT_TRUE(iv_payload.has_success());
  ASSERT_EQ(iv_payload.success().threshold_report().approved_partitions_size(), 1);
  EXPECT_EQ(iv_payload.success().threshold_report().approved_partitions(0).metric_id(),
            metric_id_b);
  EXPECT_THAT(result->threshold_filter_output.encrypted, SizeIs(2));
  // Faulted pipeline PrePadding emitted 0 padding messages (Metric B count 2 <= target 5).
  EXPECT_THAT(result->pre_padding_output.encrypted, IsEmpty());

  // Faulted pipeline ScatterShuffle emitted 1 batch (Metric B only) of 2 records.
  EXPECT_THAT(result->scatter_shuffle_output.encrypted, SizeIs(2));
  // Faulted pipeline GatherShuffle emitted 2 records.
  EXPECT_THAT(result->gather_shuffle_output.encrypted, SizeIs(2));
}

TEST(ShufflerFlowIntegrationTest, PipelineTerminatesWhenIngestUploadsEmitsIntegrityFault) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;

  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 5);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });

  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
  };

  const PipelineRouters routers{
      .ingest_uploads_output_splitter = SplitIngestUploadsOutputsWithInjectedIntegrityFault,
  };

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ShufflerFlowIntegrationTest, PipelineTerminatesWhenIngestionVerificationEmitsIntegrityFault) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;

  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 5);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });

  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
  };

  const PipelineRouters routers{
      .ingestion_verification_router = RouteToIngestionVerificationWithMismatchedRunId,
  };

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ShufflerFlowIntegrationTest, PipelineTerminatesWhenPrePaddingEmitsIntegrityFaultOnEmptyInput) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;

  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 5);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });

  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
  };

  const PipelineRouters routers{
      .pre_padding_router = RouteToPrePaddingWithEmptyInput,
  };

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ShufflerFlowIntegrationTest,
     PipelineTerminatesWhenPrePaddingEmitsIntegrityFaultOnUnexpectedRecord) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;

  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 5);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });

  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
  };

  const PipelineRouters routers{
      .pre_padding_router = RouteToPrePaddingWithUnexpectedRecord,
  };

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ShufflerFlowIntegrationTest,
     PipelineTerminatesWhenScatterShuffleEmitsIntegrityFaultOnUnderpaddedBatch) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;

  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 5);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });

  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_1",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_1",
      }),
  };

  const PipelineRouters routers{
      .scatter_shuffle_router = RouteUnderpaddedBatchToScatterShuffle,
  };

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ShufflerFlowIntegrationTest,
     PipelineTerminatesWhenScatterShuffleEmitsIntegrityFaultOnLateVerificationPayload) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;

  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 5);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });

  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
  };

  const PipelineRouters routers{
      .scatter_shuffle_router = RouteToScatterShuffleWithLateVerificationPayload,
  };

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ShufflerFlowIntegrationTest,
     PipelineTerminatesWhenScatterShuffleEmitsIntegrityFaultOnMixedPartitionBatch) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;

  TestSigningKey signing_key;
  const RunConfig run_config = CreateTestRunConfig(
      {{.project_id = project_id, .metric_id = 2}, {.project_id = project_id, .metric_id = 4}}, 0,
      10, 2);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = 2, .threshold = 2},
      {.project_id = project_id, .metric_id = 4, .threshold = 2},
  });

  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = 2,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = 2,
          .payload = "payload_1",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_1",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = 4,
          .payload = "payload_2",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_2",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = 4,
          .payload = "payload_3",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_3",
      }),
  };

  const PipelineRouters routers{
      .scatter_shuffle_router = RouteMixedPartitionBatchToScatterShuffle,
  };

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ShufflerFlowIntegrationTest, PipelineFailsWhenGatherShuffleReceivesEmptyInput) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;
  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 1);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });
  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_1",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_1",
      }),
  };

  PipelineRouters routers;
  routers.gather_shuffle_router = RouteToGatherShuffleWithEmptyInput;
  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ShufflerFlowIntegrationTest, PipelineFailsWhenGatherShuffleReceivesLateVerificationPayload) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;
  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 1);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });
  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_1",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_1",
      }),
  };

  PipelineRouters routers;
  routers.gather_shuffle_router = RouteToGatherShuffleWithLateVerificationPayload;
  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ShufflerFlowIntegrationTest, PipelineFailsWhenGatherShuffleReceivesUnexpectedRecord) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;
  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 1);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });
  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_1",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_1",
      }),
  };

  PipelineRouters routers;
  routers.gather_shuffle_router = RouteToGatherShuffleWithUnexpectedRecord;
  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ShufflerFlowIntegrationTest, PipelineFailsWhenGatherShuffleReceivesMixedGroup) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;
  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}},
                          /*min_epoch=*/0, /*max_epoch=*/10, /*batch_size=*/2);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });
  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_1",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_1",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_2",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_2",
      }),
  };

  PipelineRouters routers;
  routers.gather_shuffle_router = RouteMixedGroupToGatherShuffle;
  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ShufflerFlowIntegrationTest, PipelineGathersPartitionWhoseGroupsAreMostlyEmpty) {
  // target_batch_size 1 over 3 messages gives batch_num = 3 and
  // actual_batch_size = 1: every scatter batch holds a single message, which is
  // always at position 0 and so always lands in gather group 0. Gather groups 1
  // and 2 are therefore expected to hold nothing at all (b/564506496).
  //
  // A replica raised for one of those empty groups could not tell which group
  // it served and would fault, so this pins that the orchestrator routes by the
  // messages it observes and raises a replica only for the group that has them.
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;
  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}},
                          /*min_epoch=*/0, /*max_epoch=*/10, /*batch_size=*/1);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });
  std::vector<std::string> checkpoints;
  for (int i = 0; i < 3; ++i) {
    checkpoints.push_back(CreateCheckpointFromSpec({
        .project_id = project_id,
        .metric_id = metric_id,
        .payload = absl::StrCat("payload_", i),
        .event_time = "1970-01-01T00:00:00Z",
        .privacy_id = absl::StrCat("device_", i),
    }));
  }

  const absl::StatusOr<PipelineExecutionResult> result =
      ExecutePipeline(PipelineRouters{}, signing_key, run_config, constraints, checkpoints);
  ASSERT_THAT(result.status(), IsOk());

  // No padding is needed because 3 messages divide evenly into 3 batches.
  EXPECT_THAT(result->pre_padding_output.encrypted, IsEmpty());
  EXPECT_THAT(result->scatter_shuffle_output.encrypted, SizeIs(3));

  // The single non-empty gather group holds all 3 messages.
  EXPECT_THAT(result->gather_shuffle_output.encrypted, SizeIs(3));
  EXPECT_THAT(
      GatheredPayloads(result->gather_shuffle_output.encrypted),
      UnorderedElementsAreArray(ScatteredPayloads(result->scatter_shuffle_output.encrypted)));
}

TEST(ShufflerFlowIntegrationTest,
     EncryptOutputEmitsNonPaddingMessagesAndPartialReportsAcrossMultipleShards) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;
  TestSigningKey signing_key;
  // 3 real messages with target_batch_size = 2 -> 1 padding message (4 total in shuffle),
  // but EncryptOutput must strip the 1 padding message and emit only 3 EncryptedMessageOutputs
  // plus 2 PartialReleaseReports (1 per shard).
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}},
                          /*min_epoch=*/0, /*max_epoch=*/10, /*batch_size=*/2);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 2},
  });
  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_1",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_1",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_2",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_2",
      }),
  };

  PipelineRouters routers;
  routers.encrypt_output_router = RouteToTwoShardEncryptOutput;
  absl::StatusOr<PipelineExecutionResult> result =
      ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints);
  ASSERT_THAT(result, IsOk());

  // 3 unencrypted EncryptedMessageOutput records (padding stripped) + 2 encrypted
  // PartialReleaseReports.
  EXPECT_THAT(result->encrypt_output_output.unencrypted, SizeIs(3));
  EXPECT_THAT(result->encrypt_output_output.encrypted, SizeIs(2));
}

TEST(ShufflerFlowIntegrationTest, PipelineFailsWhenEncryptOutputReceivesEmptyInput) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;
  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}},
                          /*min_epoch=*/0, /*max_epoch=*/10, /*batch_size=*/1);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });
  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
  };

  PipelineRouters routers;
  routers.encrypt_output_router = RouteToEncryptOutputWithEmptyInput;
  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ShufflerFlowIntegrationTest, PipelineFailsWhenEncryptOutputReceivesLateVerificationPayload) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;
  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}},
                          /*min_epoch=*/0, /*max_epoch=*/10, /*batch_size=*/1);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });
  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
  };

  PipelineRouters routers;
  routers.encrypt_output_router = RouteToEncryptOutputWithLateVerificationPayload;
  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ShufflerFlowIntegrationTest, PipelineFailsWhenEncryptOutputReceivesPartiallyShuffledMessage) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;
  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}},
                          /*min_epoch=*/0, /*max_epoch=*/10, /*batch_size=*/1);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });
  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
  };

  PipelineRouters routers;
  routers.encrypt_output_router = RoutePartiallyShuffledMessageToEncryptOutput;
  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ShufflerFlowIntegrationTest,
     EndToEndFullPipelineReleasesKeyAndDecryptsExactApprovedCustomerMessages) {
  const int32_t project_id = 1001;
  const int32_t approved_metric_a = 2;
  const int32_t approved_metric_b = 4;
  const int32_t dropped_metric_c = 5;

  TestSigningKey signing_key;
  const RunConfig run_config = CreateTestRunConfig(
      {
          {.project_id = project_id, .metric_id = approved_metric_a},
          {.project_id = project_id, .metric_id = approved_metric_b},
          {.project_id = project_id, .metric_id = dropped_metric_c},
      },
      /*min_epoch=*/0, /*max_epoch=*/10, /*batch_size=*/2);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = approved_metric_a, .threshold = 2},
      {.project_id = project_id, .metric_id = approved_metric_b, .threshold = 2},
      {.project_id = project_id, .metric_id = dropped_metric_c, .threshold = 2},
  });

  // Metric A: 3 messages from 3 devices (approved; 1 padding added for batch_size=2).
  // Metric B: 2 messages from 2 devices (approved; 0 padding added).
  // Metric C: 1 message from 1 device (below threshold=2 -> dropped).
  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = approved_metric_a,
          .payload = "customer_msg_a_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "dev_a0",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = approved_metric_a,
          .payload = "customer_msg_a_1",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "dev_a1",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = approved_metric_a,
          .payload = "customer_msg_a_2",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "dev_a2",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = approved_metric_b,
          .payload = "customer_msg_b_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "dev_b0",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = approved_metric_b,
          .payload = "customer_msg_b_1",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "dev_b1",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = dropped_metric_c,
          .payload = "customer_msg_c_dropped",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "dev_c0",
      }),
  };

  PipelineRouters routers;
  routers.ingest_uploads_router = RouteToTwoShardIngestUploads;
  routers.encrypt_output_router = RouteToTwoShardEncryptOutput;

  absl::StatusOr<PipelineExecutionResult> result =
      ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints);
  ASSERT_THAT(result, IsOk());

  const MetricEpochKey key_a = CreateMetricEpochKey(
      {.project_id = project_id, .metric_id = approved_metric_a, .epoch_index = 0});
  const MetricEpochKey key_b = CreateMetricEpochKey(
      {.project_id = project_id, .metric_id = approved_metric_b, .epoch_index = 0});
  const MetricEpochKey key_c = CreateMetricEpochKey(
      {.project_id = project_id, .metric_id = dropped_metric_c, .epoch_index = 0});

  ASSERT_EQ(result->released_customer_messages.size(), 2);
  ASSERT_TRUE(result->released_customer_messages.contains(key_a));
  ASSERT_TRUE(result->released_customer_messages.contains(key_b));
  EXPECT_FALSE(result->released_customer_messages.contains(key_c));

  EXPECT_THAT(
      result->released_customer_messages.at(key_a),
      ::testing::UnorderedElementsAre("customer_msg_a_0", "customer_msg_a_1", "customer_msg_a_2"));
  EXPECT_THAT(result->released_customer_messages.at(key_b),
              ::testing::UnorderedElementsAre("customer_msg_b_0", "customer_msg_b_1"));
}

TEST(ShufflerFlowIntegrationTest,
     PipelineFailsAndWithholdsReleaseKeyWhenPartialReleaseReportIsDropped) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;
  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}},
                          /*min_epoch=*/0, /*max_epoch=*/10, /*batch_size=*/2);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 2},
  });
  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_1",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_1",
      }),
  };

  PipelineRouters routers;
  routers.encrypt_output_router = RouteToTwoShardEncryptOutput;
  routers.key_releaser_router = RouteToKeyReleaserWithDroppedReport;

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ShufflerFlowIntegrationTest, PipelineFailsWhenKeyReleaserReceivesEmptyInput) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;
  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}},
                          /*min_epoch=*/0, /*max_epoch=*/10, /*batch_size=*/1);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });
  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
  };

  PipelineRouters routers;
  routers.key_releaser_router = RouteToKeyReleaserWithEmptyInput;

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(ShufflerFlowIntegrationTest, PipelineFailsWhenKeyReleaserReceivesLateVerificationPayload) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;
  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}},
                          /*min_epoch=*/0, /*max_epoch=*/10, /*batch_size=*/1);
  const TrustedShufflerConfigConstraints constraints = CreateTestConstraints({
      {.project_id = project_id, .metric_id = metric_id, .threshold = 1},
  });
  const std::vector<std::string> checkpoints = {
      CreateCheckpointFromSpec({
          .project_id = project_id,
          .metric_id = metric_id,
          .payload = "payload_0",
          .event_time = "1970-01-01T00:00:00Z",
          .privacy_id = "device_0",
      }),
  };

  PipelineRouters routers;
  routers.key_releaser_router = RouteToKeyReleaserWithLateVerificationPayload;

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, constraints, checkpoints).status(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler::testing::integration
