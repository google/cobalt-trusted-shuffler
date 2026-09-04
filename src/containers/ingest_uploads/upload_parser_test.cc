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

#include "src/containers/ingest_uploads/upload_parser.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"
#include "fcp/confidentialcompute/client_payload.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/proto/trusted_shuffler_upload.pb.h"
#include "src/testing/matchers.h"
#include "src/testing/test_utils.h"
#include "src/util/time_utils.h"
#include "tensorflow_federated/cc/core/impl/aggregation/core/mutable_vector_data.h"
#include "tensorflow_federated/cc/core/impl/aggregation/core/tensor.h"
#include "tensorflow_federated/cc/core/impl/aggregation/protocol/checkpoint_builder.h"
#include "tensorflow_federated/cc/core/impl/aggregation/protocol/federated_compute_checkpoint_builder.h"

namespace cobalt::trusted_shuffler {
namespace {

using absl_testing::IsOk;
using absl_testing::StatusIs;
using testing::CreateTestFcCheckpoint;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using testing::MatchesUpload;
using ::testing::SizeIs;

TEST(UploadParserTest, ParsesFcCheckpointTensors) {
  TrustedShufflerUpload upload1;
  upload1.set_project_id(10);
  upload1.set_metric_id(101);
  upload1.set_serialized_payload("ckpt_payload_1");

  TrustedShufflerUpload upload2;
  upload2.set_project_id(10);
  upload2.set_metric_id(102);
  upload2.set_serialized_payload("ckpt_payload_2");

  const std::vector<std::string> event_times = {
      "2026-09-03T18:00:00Z",
      "2026-09-03T18:05:00Z",
  };
  const std::string privacy_id = "test_privacy_id_12345";

  const std::string checkpoint_data =
      CreateTestFcCheckpoint({upload1, upload2}, event_times, privacy_id);

  const absl::StatusOr<ClientUploads> parsed_uploads = ParseUploads(checkpoint_data);
  ASSERT_THAT(parsed_uploads, IsOk());

  EXPECT_THAT(parsed_uploads->privacy_id, Eq("test_privacy_id_12345"));

  const ClientUpload expected1 = {
      .upload = upload1,
      .event_time = absl::FromCivil(absl::CivilSecond(2026, 9, 3, 18, 0, 0), absl::UTCTimeZone()),
  };
  const ClientUpload expected2 = {
      .upload = upload2,
      .event_time = absl::FromCivil(absl::CivilSecond(2026, 9, 3, 18, 5, 0), absl::UTCTimeZone()),
  };

  EXPECT_THAT(parsed_uploads->uploads,
              ElementsAre(MatchesUpload(expected1), MatchesUpload(expected2)));
}

TEST(UploadParserTest, ReturnsErrorWhenCheckpointHasNoEventTimes) {
  TrustedShufflerUpload upload;
  upload.set_project_id(10);
  upload.set_metric_id(101);
  upload.set_serialized_payload("no_event_times_payload");

  const std::string checkpoint_data =
      CreateTestFcCheckpoint({upload}, /*event_times=*/{}, "test_privacy_id_no_times");

  const absl::StatusOr<ClientUploads> parsed_uploads = ParseUploads(checkpoint_data);
  EXPECT_THAT(parsed_uploads, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(UploadParserTest, ParsesCheckpointWithoutPrivacyId) {
  TrustedShufflerUpload upload;
  upload.set_project_id(10);
  upload.set_metric_id(101);
  upload.set_serialized_payload("no_privacy_id_payload");

  const std::string checkpoint_data =
      CreateTestFcCheckpoint({upload}, {"2026-09-03T18:00:00Z"}, /*privacy_id=*/std::nullopt);

  const absl::StatusOr<ClientUploads> parsed_uploads = ParseUploads(checkpoint_data);
  ASSERT_THAT(parsed_uploads, IsOk());

  EXPECT_THAT(parsed_uploads->privacy_id, IsEmpty());
  ASSERT_THAT(parsed_uploads->uploads, SizeIs(1));

  const ClientUpload expected = {
      .upload = upload,
      .event_time = absl::FromCivil(absl::CivilSecond(2026, 9, 3, 18, 0, 0), absl::UTCTimeZone()),
  };
  EXPECT_THAT(parsed_uploads->uploads, ElementsAre(MatchesUpload(expected)));
}

TEST(UploadParserTest, ReturnsErrorWhenEventTimeCountDoesNotMatchEntryCount) {
  TrustedShufflerUpload upload1;
  upload1.set_project_id(10);
  upload1.set_metric_id(101);
  upload1.set_serialized_payload("payload_1");

  TrustedShufflerUpload upload2;
  upload2.set_project_id(10);
  upload2.set_metric_id(102);
  upload2.set_serialized_payload("payload_2");

  // 2 uploads, but only 1 event time: mismatch.
  const std::string checkpoint_data =
      CreateTestFcCheckpoint({upload1, upload2}, {"2026-09-03T18:00:00Z"}, "test_privacy_id");

  const absl::StatusOr<ClientUploads> parsed_uploads = ParseUploads(checkpoint_data);
  EXPECT_THAT(parsed_uploads, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(UploadParserTest, ReturnsErrorOnMalformedEventTimeString) {
  TrustedShufflerUpload upload;
  upload.set_project_id(10);
  upload.set_metric_id(101);
  upload.set_serialized_payload("payload_data");

  const std::string checkpoint_data =
      CreateTestFcCheckpoint({upload}, {"invalid-time-format-string"}, "test_privacy_id");

  const absl::StatusOr<ClientUploads> parsed_uploads = ParseUploads(checkpoint_data);
  EXPECT_THAT(parsed_uploads, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(UploadParserTest, ReturnsErrorOnCorruptedUploadEntryProto) {
  tensorflow_federated::aggregation::FederatedComputeCheckpointBuilderFactory builder_factory;
  std::unique_ptr<tensorflow_federated::aggregation::CheckpointBuilder> builder =
      builder_factory.Create();

  const std::vector<std::string> corrupted_entries = {"\xff\xff\xff\xff_not_a_valid_proto"};
  std::vector<absl::string_view> entry_views(corrupted_entries.begin(), corrupted_entries.end());
  auto entry_data =
      std::make_unique<tensorflow_federated::aggregation::MutableVectorData<absl::string_view>>(
          std::move(entry_views));
  auto entry_tensor = tensorflow_federated::aggregation::Tensor::Create(
      tensorflow_federated::aggregation::DT_STRING,
      tensorflow_federated::aggregation::TensorShape({1}), std::move(entry_data));
  CHECK_OK(entry_tensor.status());
  CHECK_OK(builder->Add("upload_query/entry", *entry_tensor));

  const auto checkpoint = builder->Build();
  CHECK_OK(checkpoint.status());

  const absl::StatusOr<ClientUploads> parsed_uploads = ParseUploads(std::string(*checkpoint));
  EXPECT_THAT(parsed_uploads, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(UploadParserTest, ParsesEncodedClientPayloadHeaderWithCheckpoint) {
  TrustedShufflerUpload upload;
  upload.set_project_id(2);
  upload.set_metric_id(200);
  upload.set_serialized_payload("encoded_payload_data");

  const std::vector<std::string> event_times = {"2026-09-03T19:00:00Z"};
  const std::string privacy_id = "env_privacy_id_999";

  const std::string checkpoint_data = CreateTestFcCheckpoint({upload}, event_times, privacy_id);

  fcp::confidential_compute::ClientPayloadHeader header;
  header.encrypted_symmetric_key = "sym_key";
  header.encapsulated_public_key = "pub_key";
  header.serialized_blob_header = "blob_header";

  const std::string encoded =
      fcp::confidential_compute::EncodeClientPayload(header, checkpoint_data);

  const absl::StatusOr<ClientUploads> parsed_uploads = ParseUploads(encoded);
  ASSERT_THAT(parsed_uploads, IsOk());

  EXPECT_THAT(parsed_uploads->privacy_id, Eq("env_privacy_id_999"));
  ASSERT_THAT(parsed_uploads->uploads, SizeIs(1));

  const ClientUpload expected = {
      .upload = upload,
      .event_time = absl::FromCivil(absl::CivilSecond(2026, 9, 3, 19, 0, 0), absl::UTCTimeZone()),
  };
  EXPECT_THAT(parsed_uploads->uploads, ElementsAre(MatchesUpload(expected)));
}

TEST(UploadParserTest, ReturnsErrorOnRawTrustedShufflerUploadProto) {
  TrustedShufflerUpload upload;
  upload.set_project_id(1);
  upload.set_metric_id(100);
  upload.set_serialized_payload("raw_payload_data");

  // Raw TrustedShufflerUpload without FCKP checkpoint tensor stream is rejected.
  const absl::StatusOr<ClientUploads> parsed_uploads = ParseUploads(upload.SerializeAsString());
  EXPECT_THAT(parsed_uploads, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(UploadParserTest, ReturnsErrorOnInvalidPayload) {
  const absl::StatusOr<ClientUploads> parsed_uploads = ParseUploads("corrupted_invalid_data");
  EXPECT_THAT(parsed_uploads, StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
