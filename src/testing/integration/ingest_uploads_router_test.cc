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

#include "src/testing/integration/ingest_uploads_router.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "containers/session.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"
#include "gtest/gtest.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::IsEmpty;
using ::testing::SizeIs;

google::protobuf::Any PackToAny(const google::protobuf::Message& message) {
  google::protobuf::Any any;
  any.PackFrom(message);
  return any;
}

IngestMessagesHeader CreateMetricEpochHeader(const int32_t project_id, const int32_t metric_id,
                                             const int64_t epoch_index) {
  IngestMessagesHeader header;
  header.mutable_metric_epoch_key()->set_project_id(project_id);
  header.mutable_metric_epoch_key()->set_metric_id(metric_id);
  header.mutable_metric_epoch_key()->set_epoch_index(epoch_index);
  return header;
}

IngestMessagesHeader CreatePartialIngestionReportHeader() {
  IngestMessagesHeader header;
  header.mutable_partial_ingestion_report();
  return header;
}

IngestMessagesHeader CreateIntegrityFaultHeader() {
  IngestMessagesHeader header;
  header.mutable_integrity_fault();
  return header;
}

TEST(IngestUploadsRouterTest, RouteToIngestUploadsPrependsSignedRunContextAndCheckpoints) {
  const EncryptedBlob signed_run_context(Node::kRunContext, "ctx_ciphertext");
  const std::vector<std::string> checkpoints = {"cp_1", "cp_2"};

  const absl::StatusOr<std::vector<KVCollection>> result =
      RouteToIngestUploads(signed_run_context, checkpoints);
  ASSERT_THAT(result.status(), IsOk());
  ASSERT_THAT(*result, SizeIs(1));
  const KVCollection& shard = (*result)[0];
  ASSERT_THAT(shard, SizeIs(3));
  EXPECT_EQ(shard[0].node, Node::kRunContext);
  EXPECT_EQ(shard[0].data, "ctx_ciphertext");
  EXPECT_EQ(shard[1].node, Node::kClientUploads);
  EXPECT_EQ(shard[1].data, "cp_1");
  EXPECT_EQ(shard[2].node, Node::kClientUploads);
  EXPECT_EQ(shard[2].data, "cp_2");
}

TEST(IngestUploadsRouterTest, RouteToIngestUploadsLeavesEveryRecordKeyEmpty) {
  const EncryptedBlob signed_run_context(Node::kRunContext, "ctx_ciphertext");
  const std::vector<std::string> checkpoints = {"cp_1"};

  const absl::StatusOr<std::vector<KVCollection>> result =
      RouteToIngestUploads(signed_run_context, checkpoints);
  ASSERT_THAT(result.status(), IsOk());
  ASSERT_THAT(*result, SizeIs(1));
  for (const RoutedKV& input : (*result)[0]) {
    EXPECT_THAT(input.key.type_url(), IsEmpty());
  }
}

TEST(IngestUploadsRouterTest, RouteToTwoShardIngestUploadsSplitsCheckpointsAcrossShards) {
  const EncryptedBlob signed_run_context(Node::kRunContext, "ctx_ciphertext");
  const std::vector<std::string> checkpoints = {"cp_1", "cp_2", "cp_3"};

  const absl::StatusOr<std::vector<KVCollection>> result =
      RouteToTwoShardIngestUploads(signed_run_context, checkpoints);
  ASSERT_THAT(result.status(), IsOk());
  ASSERT_THAT(*result, SizeIs(2));

  // Shard 0: context + cp_1
  ASSERT_THAT((*result)[0], SizeIs(2));
  EXPECT_EQ((*result)[0][0].data, "ctx_ciphertext");
  EXPECT_EQ((*result)[0][1].data, "cp_1");

  // Shard 1: context + cp_2 + cp_3
  ASSERT_THAT((*result)[1], SizeIs(3));
  EXPECT_EQ((*result)[1][0].data, "ctx_ciphertext");
  EXPECT_EQ((*result)[1][1].data, "cp_2");
  EXPECT_EQ((*result)[1][2].data, "cp_3");
}

TEST(IngestUploadsRouterTest,
     RouteToTwoShardIngestUploadsLeavesIdleShardEmptyWhenSingleCheckpointProvided) {
  const EncryptedBlob signed_run_context(Node::kRunContext, "ctx_ciphertext");
  const std::vector<std::string> checkpoints = {"cp_1"};

  const absl::StatusOr<std::vector<KVCollection>> result =
      RouteToTwoShardIngestUploads(signed_run_context, checkpoints);
  ASSERT_THAT(result.status(), IsOk());
  ASSERT_THAT(*result, SizeIs(2));
  EXPECT_THAT((*result)[0], IsEmpty());
  ASSERT_THAT((*result)[1], SizeIs(2));
  EXPECT_EQ((*result)[1][0].data, "ctx_ciphertext");
  EXPECT_EQ((*result)[1][1].data, "cp_1");
}

TEST(IngestUploadsRouterTest, RouteToIngestUploadsWithLateRunContextPlacesRunContextLast) {
  const EncryptedBlob signed_run_context(Node::kRunContext, "ctx_ciphertext");
  const std::vector<std::string> checkpoints = {"cp_1"};

  const absl::StatusOr<std::vector<KVCollection>> result =
      RouteToIngestUploadsWithLateRunContext(signed_run_context, checkpoints);
  ASSERT_THAT(result.status(), IsOk());
  ASSERT_THAT(*result, SizeIs(1));
  const KVCollection& shard = (*result)[0];
  ASSERT_THAT(shard, SizeIs(2));

  EXPECT_EQ(shard[0].data, "cp_1");
  EXPECT_EQ(shard[1].data, "ctx_ciphertext");
}

TEST(IngestUploadsRouterTest,
     RouteToTwoShardIngestUploadsWithSecondShardAbortPlacesRunContextLastInShardOne) {
  const EncryptedBlob signed_run_context(Node::kRunContext, "ctx_ciphertext");
  const std::vector<std::string> checkpoints = {"cp_1", "cp_2"};

  const absl::StatusOr<std::vector<KVCollection>> result =
      RouteToTwoShardIngestUploadsWithSecondShardAbort(signed_run_context, checkpoints);
  ASSERT_THAT(result.status(), IsOk());
  ASSERT_THAT(*result, SizeIs(2));

  // Shard 0: context first
  EXPECT_EQ((*result)[0][0].data, "ctx_ciphertext");

  // Shard 1: context last
  EXPECT_EQ((*result)[1][1].data, "ctx_ciphertext");
}

TEST(IngestUploadsRouterTest, SplitIngestUploadsOutputsSeparatesReportsAndDropsMessageKeys) {
  const KVBlobStageOutputs ingest_output{
      .encrypted =
          {
              EncryptedKVBlob(Node::kIngestedMessages,
                              PackToAny(CreateMetricEpochHeader(
                                  /*project_id=*/1001, /*metric_id=*/2, /*epoch_index=*/5)),
                              "msg_ciphertext"),
              EncryptedKVBlob(Node::kPartialIngestionReports,
                              PackToAny(CreatePartialIngestionReportHeader()), "report_ciphertext"),
          },
  };

  const absl::StatusOr<IngestUploadsOutputs> split = SplitIngestUploadsOutputs(ingest_output);
  ASSERT_THAT(split.status(), IsOk());
  ASSERT_THAT(split->partial_reports, SizeIs(1));
  EXPECT_EQ(split->partial_reports[0].node(), Node::kPartialIngestionReports);
  EXPECT_EQ(split->partial_reports[0].ciphertext(), "report_ciphertext");

  ASSERT_THAT(split->ingested_messages, SizeIs(1));
  EXPECT_EQ(split->ingested_messages[0].node(), Node::kIngestedMessages);
  EXPECT_EQ(split->ingested_messages[0].ciphertext(), "msg_ciphertext");
}

TEST(IngestUploadsRouterTest, SplitIngestUploadsOutputsWithDroppedReportsDropsLastReport) {
  const KVBlobStageOutputs ingest_output{
      .encrypted =
          {
              EncryptedKVBlob(Node::kPartialIngestionReports,
                              PackToAny(CreatePartialIngestionReportHeader()),
                              "report_ciphertext_0"),
              EncryptedKVBlob(Node::kPartialIngestionReports,
                              PackToAny(CreatePartialIngestionReportHeader()),
                              "report_ciphertext_1"),
              EncryptedKVBlob(Node::kIngestedMessages,
                              PackToAny(CreateMetricEpochHeader(
                                  /*project_id=*/1001, /*metric_id=*/2, /*epoch_index=*/5)),
                              "msg_ciphertext"),
          },
  };

  const absl::StatusOr<IngestUploadsOutputs> split =
      SplitIngestUploadsOutputsWithDroppedReports(ingest_output);
  ASSERT_THAT(split.status(), IsOk());
  ASSERT_THAT(split->partial_reports, SizeIs(1));
  EXPECT_EQ(split->partial_reports[0].ciphertext(), "report_ciphertext_0");
  ASSERT_THAT(split->ingested_messages, SizeIs(1));
  EXPECT_EQ(split->ingested_messages[0].ciphertext(), "msg_ciphertext");
}

TEST(IngestUploadsRouterTest, SplitIngestUploadsOutputsFailsOnInvalidHeaderKey) {
  MetricEpochKey non_header_key;
  non_header_key.set_project_id(1001);
  const KVBlobStageOutputs ingest_output{
      .encrypted = {EncryptedKVBlob(Node::kIngestedMessages, PackToAny(non_header_key),
                                    "ciphertext")},
  };

  EXPECT_THAT(SplitIngestUploadsOutputs(ingest_output).status(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(IngestUploadsRouterTest, SplitIngestUploadsOutputsFailsOnEmptyIngestMessagesHeader) {
  const KVBlobStageOutputs ingest_output{
      .encrypted = {EncryptedKVBlob(Node::kIngestedMessages, PackToAny(IngestMessagesHeader()),
                                    "ciphertext")},
  };

  EXPECT_THAT(SplitIngestUploadsOutputs(ingest_output).status(),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(IngestUploadsRouterTest, SplitIngestUploadsOutputsFailsOnIntegrityFault) {
  const KVBlobStageOutputs ingest_output{
      .encrypted = {EncryptedKVBlob(Node::kIngestedMessages,
                                    PackToAny(CreateIntegrityFaultHeader()), "ciphertext")},
  };

  EXPECT_THAT(SplitIngestUploadsOutputs(ingest_output).status(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(IngestUploadsRouterTest, SplitIngestUploadsOutputsWithInjectedIntegrityFaultFails) {
  const KVBlobStageOutputs ingest_output{
      .encrypted = {EncryptedKVBlob(Node::kPartialIngestionReports,
                                    PackToAny(CreatePartialIngestionReportHeader()), "ciphertext")},
  };

  EXPECT_THAT(SplitIngestUploadsOutputsWithInjectedIntegrityFault(ingest_output).status(),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(IngestUploadsRouterTest, DroppedReportSplitterDropsPartialReports) {
  const KVBlobStageOutputs ingest_output{
      .encrypted = {EncryptedKVBlob(Node::kPartialIngestionReports,
                                    PackToAny(CreatePartialIngestionReportHeader()),
                                    "report_ciphertext")},
  };

  const absl::StatusOr<IngestUploadsOutputs> split =
      SplitIngestUploadsOutputsWithDroppedReports(ingest_output);
  ASSERT_THAT(split.status(), IsOk());
  EXPECT_THAT(split->partial_reports, IsEmpty());
}

}  // namespace
}  // namespace cobalt::trusted_shuffler::testing::integration
