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

#ifndef SRC_CONTAINERS_SCATTER_SHUFFLE_SCATTER_SHUFFLE_FN_H_
#define SRC_CONTAINERS_SCATTER_SHUFFLE_SCATTER_SHUFFLE_FN_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "google/protobuf/any.pb.h"
#include "src/batching/batch_calculator.h"
#include "src/containers/base/first_record_config_do_fn.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/proto/scatter_shuffle.pb.h"

namespace cobalt::trusted_shuffler {

// Local output node index for re-encryption for the downstream gather shuffle stage.
inline constexpr int kReencryptionKeyIndex = 0;

// DoFn implementation for the scatter_shuffle container.
//
// Performs scatter shuffle and group assignment (two-pass shuffling phase 1):
// 1. First record (Record 0): Ingests IngestionVerificationResult and validates:
//    - SignedRunContext is present, parses RunContext, and has positive target_batch_size.
//    - ThresholdReport has approved partitions.
// 2. Subsequent records (Records 1..N): Ingests encrypted KV records (MetricEpochKey header,
//    ShuffledMessage payload, including real encrypted messages and dummy padding messages).
//    - Validates that the record header key is an approved partition.
//    - Calculates batching parameters (target_batch_size, batch_num, padding_count) from the
//      IngestionVerificationResult using the batch calculator library.
//    - Enforces that all records share identical MetricEpochKey, run_id, and shuffle_count (0).
// 3. FinalizeReplica:
//    - Verifies that the replica received exactly `target_batch_size` messages.
//    - If any validation or batch size check fails, signals IntegrityFault.
//    - Cryptographically shuffles the batch using Fisher-Yates CSPRNG shuffling.
//    - Assigns each message a group_id equal to (new_position % batch_num).
//    - Increments shuffle_count to 1.
//    - Re-encrypts payloads under local output node index kReencryptionKeyIndex (0)
//      and emits with visible unencrypted ScatterShuffleHeader (group_id).
class ScatterShuffleFn : public FirstRecordConfigDoFn<IngestionVerificationResult> {
 public:
  ScatterShuffleFn() = default;
  ~ScatterShuffleFn() override = default;

  // Finalizes the replica session: validates batch size, cryptographically shuffles,
  // assigns group IDs, increments shuffle_count, and emits re-encrypted records.
  absl::Status FinalizeReplica(google::protobuf::Any config, Context& context) override;

  // Accessor for testing and inspection.
  const std::optional<BatchParameters>& CurrentBatchParameters() const { return batch_parameters_; }

 protected:
  // Ingests IngestionVerificationResult on record 0 and validates run context and threshold report.
  absl::Status OnConfig(const IngestionVerificationResult& ingestion_verification_result,
                        Context& context) override;

  // Ingests incoming batch messages (records 1..N), validating invariants and establishing batch
  // parameters.
  absl::Status OnRecord(KV record, Context& context) override;

 private:
  // Signals an integrity violation by emitting an unencrypted IntegrityFault record,
  // logging an error on Context, and returning a FailedPreconditionError.
  absl::Status SignalIntegrityFault(Context& context);

  std::optional<IngestionVerificationResult> ingestion_verification_result_;
  std::optional<RunContext> run_context_;
  std::optional<BatchParameters> batch_parameters_;
  std::optional<MetricEpochKey> expected_metric_epoch_key_;
  std::vector<ShuffledMessage> messages_;

  std::optional<std::string> fault_reason_;
  bool fault_emitted_ = false;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_SCATTER_SHUFFLE_SCATTER_SHUFFLE_FN_H_
