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

#ifndef SRC_CONTAINERS_INGESTION_VERIFICATION_INGESTION_VERIFICATION_FN_H_
#define SRC_CONTAINERS_INGESTION_VERIFICATION_INGESTION_VERIFICATION_FN_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "google/protobuf/any.pb.h"
#include "src/containers/base/first_record_config_do_fn.h"
#include "src/containers/base/integrity_fault.h"
#include "src/containers/base/pipeline_nodes.h"
#include "src/crypto/multiset_fingerprint.h"
#include "src/proto/common.pb.h"
#include "src/proto/config_constraints.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/sketch/hll_sketch.h"

namespace cobalt::trusted_shuffler {

// A DoFn class for ingestion verification in Trusted Shuffler.
//
// Inherits Do(KV input, Context& context) from FirstRecordConfigDoFn:
// - Record 0: Ingests SignedRunContext via OnConfig(), parses the inner
//   RunContext proto, extracts run_id, and initializes MultisetFingerprint.
// - Records 1..N: Ingests PartialIngestionReport protos from IngestUploads workers via
//   OnRecord(), merging partial multiset fingerprints and HyperLogLog sketches
//   per MetricEpochKey partition.
// - FinalizeReplica: Evaluates unique device cardinality estimates against
//   TrustedShufflerConfigConstraints thresholds. Emits unencrypted
//   IngestionVerificationHeader (with IngestionResult or IntegrityFault)
//   for the orchestrator, and encrypted IngestionVerificationPayload (with
//   ThresholdReport, canonical RunFingerprint, and SignedRunContext, or
//   IntegrityFault) for ThresholdFilter.
class IngestionVerificationFn
    : public FirstRecordConfigDoFn<SignedRunContext, PartialIngestionReport>::WithOutputs<
          EncryptsTo<Node::kVerifiedIngestion>,
          EncryptedKV<IngestionVerificationHeader, IngestionVerificationPayload,
                      Node::kVerifiedIngestion>,
          UnencryptedKV<IngestionVerificationHeader, IntegrityFault>> {
 public:
  explicit IngestionVerificationFn(TrustedShufflerConfigConstraints config_constraints);

  ~IngestionVerificationFn() override = default;

  // Does any setup work needed for this Fn replica.
  // Logs the project id, metric id, and threshold combinations from config constraints.
  absl::Status InitializeReplica(google::protobuf::Any config, Context& context) override;

  // Evaluates unique device counts against reporting thresholds, constructs
  // canonical fingerprint and threshold report, and emits dual verification output.
  absl::Status FinalizeReplica(google::protobuf::Any config, FnContext& context) override;

  const TrustedShufflerConfigConstraints& ConfigConstraints() const { return config_constraints_; }

 protected:
  // Ingests SignedRunContext on record 0, parses the inner RunContext, and initializes
  // fingerprinting.
  absl::Status OnConfig(const cobalt::trusted_shuffler::SignedRunContext& signed_run_context,
                        DoContext& context) override;

  // Ingests incoming PartialIngestionReport records from IngestUploads workers.
  absl::Status OnRecord(const PartialIngestionReport& report, DoContext& context) override;

 private:
  using FnContext = confidential_federated_compute::fns::Fn::FnContext;
  using DoContext = confidential_federated_compute::fns::DoFn::DoContext;

  // Project ID and Metric ID pair identifying a metric configuration.
  using MetricKey = std::pair<int32_t, int32_t>;

  // Project ID, Metric ID, and Epoch Index tuple identifying a data partition.
  using MetricEpochTuple = std::tuple<int32_t, int32_t, int64_t>;

  TrustedShufflerConfigConstraints config_constraints_;
  absl::flat_hash_map<MetricKey, int32_t> threshold_map_;

  std::string run_id_;
  std::optional<MultisetFingerprint> multiset_fingerprint_;
  std::map<MetricEpochTuple, HllSketch> partition_sketches_;
  std::map<MetricEpochTuple, MetricEpochKey> partition_keys_;
  IntegrityFaultHandler<IngestionVerificationHeader> fault_handler_;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_INGESTION_VERIFICATION_INGESTION_VERIFICATION_FN_H_
