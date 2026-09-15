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

#ifndef SRC_CONTAINERS_THRESHOLD_FILTER_THRESHOLD_FILTER_FN_H_
#define SRC_CONTAINERS_THRESHOLD_FILTER_THRESHOLD_FILTER_FN_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "google/protobuf/any.pb.h"
#include "src/containers/base/first_record_config_do_fn.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler {

// A DoFn class for threshold_filter.
//
// Inside a confidential TEE enclave:
// - Record 0: Ingests IngestionVerificationPayload via FirstRecordConfigDoFn::OnConfig().
//   Extracts SignedRunContext from IngestionVerificationResult, verifies the cryptographic
//   signature and runtime parameters, validates the run ID against ThresholdReport, and
//   populates approved partitions that met the device threshold.
// - Records 1..N: Ingests ShuffledMessages from partition grouping via OnRecord().
//   Validates that the message run_id matches RunContext and shuffle_count is 0.
//   Forwards ShuffledMessages matching approved partitions encrypted under key index 0
//   (kReencryptionKeyIndex) with unencrypted MetricEpochKey headers. Messages for
//   unapproved partitions are discarded.
class ThresholdFilterFn
    : public FirstRecordConfigDoFn<IngestionVerificationPayload, ShuffledMessage> {
 public:
  inline static constexpr int kReencryptionKeyIndex = 0;

  ThresholdFilterFn() = default;

  ~ThresholdFilterFn() override = default;

  // Finalizes the replica, verifying that configuration was successfully received.
  absl::Status FinalizeReplica(google::protobuf::Any config, Context& context) override;

  // Accessors.
  const std::optional<cobalt::trusted_shuffler::RunContext>& RunContext() const {
    return run_context_;
  }
  const std::optional<cobalt::trusted_shuffler::SignedRunContext>& SignedRunContext() const {
    return signed_run_context_;
  }
  const std::optional<IngestionVerificationPayload>& VerificationPayload() const {
    return Config();
  }
  bool HasIntegrityFault() const { return has_integrity_fault_; }
  bool HasVerificationReport() const { return has_verification_report_; }
  const absl::flat_hash_set<MetricEpochKey>& ApprovedPartitions() const {
    return approved_partitions_;
  }
  size_t ApprovedPartitionCount() const { return approved_partitions_.size(); }
  size_t BufferedMessageCount() const { return 0; }

 protected:
  // Ingests IngestionVerificationPayload on record 0.
  absl::Status OnConfig(const IngestionVerificationPayload& config, Context& context) override;

  // Ingests incoming customer data records (Records 1..N) from partition grouping.
  absl::Status OnRecord(const ShuffledMessage& shuffled_message, Context& context) override;

 private:
  // Emits an unencrypted IntegrityFault record to notify the untrusted orchestrator.
  void EmitIntegrityFault(Context& context);

  std::optional<cobalt::trusted_shuffler::RunContext> run_context_;
  std::optional<cobalt::trusted_shuffler::SignedRunContext> signed_run_context_;
  absl::flat_hash_set<MetricEpochKey> approved_partitions_;
  bool has_integrity_fault_ = false;
  bool has_verification_report_ = false;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_THRESHOLD_FILTER_THRESHOLD_FILTER_FN_H_
