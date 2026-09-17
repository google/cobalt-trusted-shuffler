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
#include "src/proto/run_context.pb.h"
#include "src/proto/trusted_shuffler_upload.pb.h"
#include "src/testing/integration/ingest_uploads_router.h"
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

TEST(ShufflerFlowIntegrationTest, EndToEndSingleSessionFlowIngestsUploadsAndEmitsReport) {
  const int32_t project_id = 1001;
  const int32_t metric_id_above_threshold = 2;
  const int32_t metric_id_below_threshold = 3;
  const int64_t min_epoch_index = 0;
  const int64_t max_epoch_index = 10;
  const int32_t target_batch_size = 5;

  TestSigningKey signing_key;
  const RunConfig run_config = CreateTestRunConfig(
      {
          {.project_id = project_id, .metric_id = metric_id_above_threshold},
          {.project_id = project_id, .metric_id = metric_id_below_threshold},
      },
      min_epoch_index, max_epoch_index, target_batch_size);

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
      ExecutePipeline(PipelineRouters{}, signing_key, run_config, checkpoints);
  ASSERT_THAT(result.status(), IsOk());

  // IngestUploads emitted 5 message records + 1 partial report = 6 encrypted records.
  EXPECT_THAT(result->ingest_uploads_output.encrypted, SizeIs(6));
}

TEST(ShufflerFlowIntegrationTest, PipelineProcessesCheckpointsAcrossMultipleIngestUploadsShards) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;

  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 5);

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
      ExecutePipeline(routers, signing_key, run_config, checkpoints);
  ASSERT_THAT(result.status(), IsOk());

  // IngestUploads across 2 shards emitted 4 messages + 2 partial reports (1 per shard) = 6 records.
  EXPECT_THAT(result->ingest_uploads_output.encrypted, SizeIs(6));

  absl::StatusOr<IngestUploadsOutputs> split =
      SplitIngestUploadsOutputs(result->ingest_uploads_output);
  ASSERT_THAT(split.status(), IsOk());
  EXPECT_THAT(split->partition_messages, SizeIs(4));
  EXPECT_THAT(split->partial_reports, SizeIs(2));
}

TEST(ShufflerFlowIntegrationTest, PipelineFailsWhenSignedRunContextNotFirstInput) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;

  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 5);

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

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, checkpoints).status(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(ShufflerFlowIntegrationTest, PipelineFailsWhenSecondShardAbortsInMultiShardStage) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;

  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 5);

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

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, checkpoints).status(),
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
      // 5 & 6. Two valid checkpoints
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
      ExecutePipeline(PipelineRouters{}, signing_key, run_config, checkpoints);
  ASSERT_THAT(result.status(), IsOk());

  // Only the 2 valid messages + 1 partial report should be emitted by IngestUploads.
  EXPECT_THAT(result->ingest_uploads_output.encrypted, SizeIs(3));
}

TEST(ShufflerFlowIntegrationTest, PipelineTerminatesWhenIngestUploadsEmitsIntegrityFault) {
  const int32_t project_id = 1001;
  const int32_t metric_id = 2;

  TestSigningKey signing_key;
  const RunConfig run_config =
      CreateTestRunConfig({{.project_id = project_id, .metric_id = metric_id}}, 0, 10, 5);

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

  EXPECT_THAT(ExecutePipeline(routers, signing_key, run_config, checkpoints).status(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler::testing::integration
