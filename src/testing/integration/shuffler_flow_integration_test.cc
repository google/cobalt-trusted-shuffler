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
#include "src/proto/config_constraints.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/proto/trusted_shuffler_upload.pb.h"
#include "src/testing/integration/ingest_uploads_router.h"
#include "src/testing/integration/ingestion_verification_router.h"
#include "src/testing/integration/pipeline_runner.h"
#include "src/testing/integration/records.h"
#include "src/testing/test_utils.h"

namespace cobalt::trusted_shuffler::testing::integration {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::SizeIs;

// Deterministic signing key implementation for testing.
class TestSigningKey : public oak::crypto::SigningKeyHandle {
 public:
  absl::StatusOr<oak::crypto::v1::Signature> Sign(absl::string_view /*message*/) override {
    oak::crypto::v1::Signature sig;
    sig.set_signature("test_signature_bytes");
    return sig;
  }
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

TEST(ShufflerFlowIntegrationTest,
     EndToEndSingleSessionFlowVerifiesIngestionAndEvaluatesThresholds) {
  const int32_t project_id = 1001;
  const int32_t metric_id_above_threshold = 2;
  const int32_t metric_id_below_threshold = 3;
  const int64_t min_epoch_index = 0;
  const int64_t max_epoch_index = 10;
  const int32_t target_batch_size = 5;
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

  // Create 5 client checkpoints:
  // - 3 uploads for metric_id_above_threshold from 3 unique devices (meets threshold of 3 ->
  // approved)
  // - 2 uploads for metric_id_below_threshold from 2 unique devices (below threshold of 3 ->
  // dropped)
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
}

TEST(ShufflerFlowIntegrationTest, EndToEndMultipleMetricEpochKeysApprovesAllEligiblePartitions) {
  const int32_t project_id = 1001;
  const int32_t metric_id_a = 2;
  const int32_t metric_id_b = 4;
  const int64_t min_epoch_index = 0;
  const int64_t max_epoch_index = 10;
  const int32_t target_batch_size = 4;
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
  EXPECT_THAT(split->partition_messages, SizeIs(4));
  EXPECT_THAT(split->partial_reports, SizeIs(2));

  // IngestionVerification processes both partial reports and approves the partition.
  ASSERT_THAT(result->ingestion_verification_output.encrypted, SizeIs(1));
  IngestionVerificationPayload iv_payload;
  ASSERT_TRUE(
      iv_payload.ParseFromString(result->ingestion_verification_output.encrypted[0].ciphertext()));
  ASSERT_TRUE(iv_payload.has_success());
  EXPECT_EQ(iv_payload.success().threshold_report().approved_partitions_size(), 1);
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
  ASSERT_THAT(result->ingestion_verification_output.encrypted, SizeIs(1));
  IngestionVerificationPayload iv_payload;
  ASSERT_TRUE(
      iv_payload.ParseFromString(result->ingestion_verification_output.encrypted[0].ciphertext()));
  ASSERT_TRUE(iv_payload.has_success());
  EXPECT_EQ(iv_payload.success().threshold_report().approved_partitions_size(), 1);
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
  // and Metric B (2 >= 2) meet threshold.
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

  // Now drop Shard 1's partial report. Metric B (2 devices in Shard 0) still meets threshold 2,
  // whereas Metric A drops from 4 unique devices to 1 (below threshold 4) and is rejected.
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

}  // namespace
}  // namespace cobalt::trusted_shuffler::testing::integration
