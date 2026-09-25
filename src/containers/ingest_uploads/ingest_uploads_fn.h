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

#ifndef SRC_CONTAINERS_INGEST_UPLOADS_INGEST_UPLOADS_FN_H_
#define SRC_CONTAINERS_INGEST_UPLOADS_INGEST_UPLOADS_FN_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "google/protobuf/any.pb.h"
#include "src/containers/base/first_record_config_do_fn.h"
#include "src/containers/base/pipeline_nodes.h"
#include "src/crypto/multiset_fingerprint.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/sketch/hll_sketch.h"
#include "src/util/time_utils.h"

namespace cobalt::trusted_shuffler {

// A DoFn class for ingesting client device uploads.
//
// Inherits Do(KV input, Context& context) from
// FirstRecordConfigDoFn<SignedRunContext, std::string>:
// - Record 0: Ingests SignedRunContext via OnConfig(), parses the inner RunContext,
//   and initializes multiset fingerprinting for the run.
// - Records 1..N: Ingests client log upload checkpoints via OnRecord(), validates metric/epoch,
//   updates HLL sketches and multiset fingerprint, and emits encrypted ShuffledMessage records.
class IngestUploadsFn
    : public FirstRecordConfigDoFn<SignedRunContext, std::string>::WithOutputs<
          EncryptsTo<Node::kIngestedMessages, Node::kPartialIngestionReports>,
          EncryptedKV<IngestMessagesHeader, ShuffledMessage, Node::kIngestedMessages>,
          EncryptedKV<IngestMessagesHeader, PartialIngestionReport,
                      Node::kPartialIngestionReports>> {
 public:
  // Project ID, Metric ID, and Epoch Index tuple identifying a data partition.
  using MetricEpochTuple = std::tuple<int32_t, int32_t, int64_t>;

  IngestUploadsFn() = default;

  bool IsMetricAllowed(const int32_t project_id, const int32_t metric_id) const;
  bool IsEpochInBounds(Epoch epoch) const;

  const RunContext& GetRunContext() const { return run_context_; }

 protected:
  // Ingests SignedRunContext on record 0 and initializes session state.
  absl::Status OnConfig(const SignedRunContext& signed_run_context, DoContext& context) override;

  // Ingests incoming client log records on records 1..N.
  absl::Status OnRecord(const std::string& record_data, DoContext& context) override;

  // Finalizes this Fn replica, building and emitting the PartialIngestionReport.
  absl::Status OnFinalize(google::protobuf::Any config, FnContext& context) override;

 private:
  using FnContext = confidential_federated_compute::fns::Fn::FnContext;
  using DoContext = confidential_federated_compute::fns::DoFn::DoContext;

  RunContext run_context_;
  absl::flat_hash_set<std::pair<int32_t, int32_t>> allowed_metric_identifiers_;
  std::optional<MultisetFingerprint> multiset_fingerprint_;
  std::map<MetricEpochTuple, HllSketch> sketches_;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_INGEST_UPLOADS_INGEST_UPLOADS_FN_H_
