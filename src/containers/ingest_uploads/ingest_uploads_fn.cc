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
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "src/containers/base/counter_names.h"
#include "src/containers/ingest_uploads/upload_parser.h"
#include "src/crypto/verify_signed_run_context.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/proto/trusted_shuffler_upload.pb.h"
#include "src/sketch/hll_sketch.h"

namespace cobalt::trusted_shuffler {

absl::Status IngestUploadsFn::OnConfig(const SignedRunContext& signed_run_context,
                                       DoContext& context) {
  context.IncrementCounter(kInputConfigCount);
  absl::StatusOr<RunContext> verified_run_context = VerifySignedRunContext(signed_run_context);
  if (!verified_run_context.ok()) {
    context.IncrementCounter(kInputConfigInvalid);
    return verified_run_context.status();
  }
  run_context_ = *std::move(verified_run_context);
  allowed_metric_identifiers_.clear();
  for (const RunConfig::MetricIdentifier& id : run_context_.run_config().metric_identifiers()) {
    allowed_metric_identifiers_.insert({id.project_id(), id.metric_id()});
  }
  multiset_fingerprint_.emplace(STAGE_INGESTION, /*is_partial=*/true, run_context_,
                                MultisetFingerprint::kFingerprintSize);
  return absl::OkStatus();
}

bool IngestUploadsFn::IsMetricAllowed(const int32_t project_id, const int32_t metric_id) const {
  return allowed_metric_identifiers_.contains({project_id, metric_id});
}

bool IngestUploadsFn::IsEpochInBounds(const Epoch epoch) const {
  if (!run_context_.has_run_config()) {
    return false;
  }
  const RunConfig& run_config = run_context_.run_config();
  return epoch >= Epoch::FromDays(run_config.min_epoch_index()) &&
         epoch <= Epoch::FromDays(run_config.max_epoch_index());
}

absl::Status IngestUploadsFn::OnRecord(const fcp::confidentialcompute::BlobData& blob_data,
                                       DoContext& context) {
  context.IncrementCounter(kInputRecordCount);
  const std::string payload(blob_data.data());
  absl::StatusOr<ClientUploads> uploads = ParseUploads(payload);
  // If the checkpoint is invalid or empty, drop it and record dropped message.
  if (!uploads.ok()) {
    context.IncrementCounter(kInputRecordDropped);
    return absl::OkStatus();
  }
  if (uploads->privacy_id.empty()) {
    context.IncrementCounter(kInputRecordDropped);
    return absl::OkStatus();
  }

  for (const ClientUpload& parsed_upload : uploads->uploads) {
    const TrustedShufflerUpload& upload = parsed_upload.upload;
    const int32_t project_id = upload.project_id();
    const int32_t metric_id = upload.metric_id();

    if (!IsMetricAllowed(project_id, metric_id)) {
      context.IncrementCounter(kInputRecordDropped);
      continue;
    }

    const Epoch epoch = Epoch::FromTime(parsed_upload.event_time);

    if (!IsEpochInBounds(epoch)) {
      context.IncrementCounter(kInputRecordDropped);
      continue;
    }

    MetricEpochKey metric_epoch_key;
    metric_epoch_key.set_project_id(project_id);
    metric_epoch_key.set_metric_id(metric_id);
    metric_epoch_key.set_epoch_index(epoch.Days());

    ShuffledMessage shuffled_message;
    *shuffled_message.mutable_metric_epoch_key() = metric_epoch_key;
    shuffled_message.set_run_id(run_context_.run_id());
    shuffled_message.set_is_padding(false);
    shuffled_message.set_shuffle_count(0);
    shuffled_message.set_customer_message(upload.serialized_payload());

    multiset_fingerprint_->AddMessage(shuffled_message);

    const MetricEpochTuple tuple_key = std::make_tuple(project_id, metric_id, epoch.Days());
    std::map<MetricEpochTuple, HllSketch>::iterator it = sketches_.find(tuple_key);
    if (it == sketches_.end()) {
      absl::StatusOr<HllSketch> created_sketch = HllSketch::Create(HllSketch::kDefaultPrecision);
      if (!created_sketch.ok()) {
        context.IncrementCounter(kInputRecordDropped);
        return created_sketch.status();
      }
      it = sketches_.emplace(tuple_key, std::move(created_sketch.value())).first;
    }
    it->second.Insert(uploads->privacy_id);

    IngestMessagesHeader header;
    *header.mutable_metric_epoch_key() = metric_epoch_key;

    google::protobuf::Any key;
    key.PackFrom(header);

    // Emits encrypted message.
    context.IncrementCounter(kEncryptedOutputRecordCount);
    context.EmitEncrypted(message_key_index_,
                          KV(std::move(key), shuffled_message.SerializeAsString()));
  }

  return absl::OkStatus();
}

absl::Status IngestUploadsFn::FinalizeReplica(google::protobuf::Any config, FnContext& context) {
  if (!multiset_fingerprint_.has_value()) {
    return absl::FailedPreconditionError(
        "RunContext configuration was not received before FinalizeReplica");
  }
  PartialIngestionReport report;
  *report.mutable_partial_fingerprint() = multiset_fingerprint_->Build();

  for (const auto& [tuple_key, sketch] : sketches_) {
    const auto& [project_id, metric_id, epoch_index] = tuple_key;
    MetricEpochSketch* metric_epoch_sketch = report.add_metric_epoch_sketches();
    MetricEpochKey* key = metric_epoch_sketch->mutable_metric_epoch_key();
    key->set_project_id(project_id);
    key->set_metric_id(metric_id);
    key->set_epoch_index(epoch_index);
    *metric_epoch_sketch->mutable_hyperloglog_sketch() = sketch.ToProto();
  }

  IngestMessagesHeader report_header;
  report_header.mutable_partial_ingestion_report();

  google::protobuf::Any key;
  key.PackFrom(report_header);

  // Emits encrypted partial report.
  context.IncrementCounter(kEncryptedOutputRecordCount);
  context.EmitEncrypted(report_key_index_, KV(std::move(key), report.SerializeAsString()));

  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler
