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

#ifndef SRC_CONTAINERS_INGEST_UPLOADS_UPLOAD_PARSER_H_
#define SRC_CONTAINERS_INGEST_UPLOADS_UPLOAD_PARSER_H_

#include <optional>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "src/proto/trusted_shuffler_upload.pb.h"
#include "src/util/time_utils.h"

namespace cobalt::trusted_shuffler {

// A single parsed client upload record, containing the record payload and event time.
struct ClientUpload {
  TrustedShufflerUpload upload;

  // Event time recorded in parallel with the upload entry (from the
  // "upload_query/confidential_compute_event_time" tensor).
  absl::Time event_time;
};

// The result of parsing a client upload payload. All records in a checkpoint
// share a single privacy id.
struct ClientUploads {
  std::vector<ClientUpload> uploads;

  // Privacy id shared by all records in the checkpoint (from the
  // "confidential_compute_privacy_id" tensor). Empty if the source checkpoint
  // did not include a privacy id.
  std::string privacy_id;
};

// Parses an incoming client upload payload into a ClientUploads bundle.
// Upload payloads are structured as Federated Compute wire format
// checkpoints ("FCKP"), which carry:
// - "upload_query/entry" string tensor (serialized TrustedShufflerUpload messages)
// - "upload_query/confidential_compute_event_time" string tensor (parallel RFC3339 event
// timestamps)
// - "confidential_compute_privacy_id" string tensor (shared privacy identifier)
//
// Payload may be passed directly as an FCKP checkpoint or wrapped inside a
// ClientPayload envelope (Confidential Aggregations protocol).
absl::StatusOr<ClientUploads> ParseUploads(absl::string_view payload);

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_INGEST_UPLOADS_UPLOAD_PARSER_H_
