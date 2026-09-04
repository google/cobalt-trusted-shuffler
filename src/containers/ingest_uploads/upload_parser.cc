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

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "fcp/confidentialcompute/client_payload.h"
#include "src/proto/trusted_shuffler_upload.pb.h"
#include "src/util/time_utils.h"
#include "tensorflow_federated/cc/core/impl/aggregation/core/tensor.h"
#include "tensorflow_federated/cc/core/impl/aggregation/protocol/checkpoint_parser.h"
#include "tensorflow_federated/cc/core/impl/aggregation/protocol/federated_compute_checkpoint_parser.h"

namespace cobalt::trusted_shuffler {
namespace {

constexpr absl::string_view kUploadEntryTensorName = "upload_query/entry";
constexpr absl::string_view kEventTimeTensorName = "upload_query/confidential_compute_event_time";
constexpr absl::string_view kPrivacyIdTensorName = "confidential_compute_privacy_id";

absl::StatusOr<ClientUploads> ParseCheckpointTensors(const absl::string_view payload) {
  tensorflow_federated::aggregation::FederatedComputeCheckpointParserFactory parser_factory;
  const absl::StatusOr<std::unique_ptr<tensorflow_federated::aggregation::CheckpointParser>>
      parser = parser_factory.Create(absl::Cord(payload));
  if (!parser.ok()) {
    return parser.status();
  }

  const absl::StatusOr<tensorflow_federated::aggregation::Tensor> entry_tensor =
      (*parser)->GetTensor(std::string(kUploadEntryTensorName));
  if (!entry_tensor.ok() || entry_tensor->dtype() != tensorflow_federated::aggregation::DT_STRING ||
      entry_tensor->num_elements() == 0) {
    return absl::InvalidArgumentError("No valid uploads found in checkpoint");
  }

  std::vector<TrustedShufflerUpload> uploads;
  uploads.reserve(entry_tensor->num_elements());
  for (const auto& [index, value] : entry_tensor->AsAggVector<absl::string_view>()) {
    TrustedShufflerUpload upload;
    if (!upload.ParseFromArray(value.data(), value.size())) {
      return absl::InvalidArgumentError(
          "Failed to parse TrustedShufflerUpload from checkpoint entry");
    }
    uploads.push_back(std::move(upload));
  }

  std::vector<std::string> event_times;
  const absl::StatusOr<tensorflow_federated::aggregation::Tensor> time_tensor =
      (*parser)->GetTensor(std::string(kEventTimeTensorName));
  if (time_tensor.ok() && time_tensor->dtype() == tensorflow_federated::aggregation::DT_STRING) {
    event_times.reserve(time_tensor->num_elements());
    for (const auto& [index, value] : time_tensor->AsAggVector<absl::string_view>()) {
      event_times.emplace_back(value);
    }
  }

  // Event times are stored in parallel with the upload entries.
  // There must be an equal number of messages and event times.
  if (event_times.size() != uploads.size()) {
    return absl::InvalidArgumentError(
        "Checkpoint event time count does not match upload entry count");
  }

  std::string privacy_id;
  const absl::StatusOr<tensorflow_federated::aggregation::Tensor> privacy_tensor =
      (*parser)->GetTensor(std::string(kPrivacyIdTensorName));
  if (privacy_tensor.ok() &&
      privacy_tensor->dtype() == tensorflow_federated::aggregation::DT_STRING &&
      privacy_tensor->num_elements() > 0) {
    for (const auto& [index, value] : privacy_tensor->AsAggVector<absl::string_view>()) {
      privacy_id = std::string(value);
      break;
    }
  }

  ClientUploads result;
  result.privacy_id = std::move(privacy_id);
  result.uploads.reserve(uploads.size());
  for (size_t i = 0; i < uploads.size(); ++i) {
    if (event_times[i].empty()) {
      return absl::InvalidArgumentError("Event time in checkpoint must not be empty");
    }
    const absl::StatusOr<absl::Time> parsed_time = ParseTime(event_times[i]);
    if (!parsed_time.ok()) {
      return absl::InvalidArgumentError(absl::StrCat("Failed to parse event time '", event_times[i],
                                                     "': ", parsed_time.status().message()));
    }
    ClientUpload client_upload;
    client_upload.upload = std::move(uploads[i]);
    client_upload.event_time = *parsed_time;
    result.uploads.push_back(std::move(client_upload));
  }
  return result;
}

}  // namespace

absl::StatusOr<ClientUploads> ParseUploads(absl::string_view payload) {
  // If wrapped in a ClientPayload envelope, strip the envelope header.
  const absl::StatusOr<fcp::confidential_compute::ClientPayloadHeader> header =
      fcp::confidential_compute::DecodeAndConsumeClientPayloadHeader(payload);
  return ParseCheckpointTensors(payload);
}

}  // namespace cobalt::trusted_shuffler
