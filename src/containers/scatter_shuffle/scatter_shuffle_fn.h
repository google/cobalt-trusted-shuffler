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
#include "src/containers/base/integrity_fault.h"
#include "src/containers/base/pipeline_nodes.h"
#include "src/crypto/verify_ingestion_verification_payload.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/proto/scatter_shuffle.pb.h"

namespace cobalt::trusted_shuffler {

// DoFn implementation for the scatter_shuffle container.
//
// Performs scatter shuffle and group assignment (two-pass shuffling phase 1):
// 1. First record (Record 0): Ingests IngestionVerificationPayload and validates it.
// 2. Subsequent records (Records 1..N): Ingests encrypted KV records (MetricEpochKey header,
//    ShuffledMessage payload, including real encrypted messages and dummy padding messages).
//    - Validates that the record header key is an approved partition.
//    - Calculates batching parameters (target_batch_size, actual_batch_size, batch_num,
//    padding_count) from the
//      VerifiedIngestionVerificationResult using the batch calculator library.
//    - Enforces that all records share identical MetricEpochKey, run_id, and shuffle_count (0).
// 3. FinalizeReplica:
//    - Verifies that the replica received exactly `actual_batch_size` messages.
//    - If any validation or batch size check fails, signals IntegrityFault.
//    - Cryptographically shuffles the batch using Fisher-Yates CSPRNG shuffling.
//    - Assigns each message a group_id equal to (new_position % batch_num).
//    - Increments shuffle_count to 1.
//    - Re-encrypts payloads under local output node index 0
//      and emits with visible unencrypted ScatterShuffleHeader (group_id).
class ScatterShuffleFn
    : public FirstRecordConfigDoFn<IngestionVerificationPayload, ShuffledMessage>::WithOutputs<
          EncryptsTo<Node::kScatteredMessages>,
          EncryptedKV<ScatterShuffleHeader, ShuffledMessage, Node::kScatteredMessages>,
          UnencryptedKV<ScatterShuffleHeader, IntegrityFault>> {
 public:
  ScatterShuffleFn();
  ~ScatterShuffleFn() override = default;

  // Accessor for testing and inspection.
  const std::optional<BatchParameters>& CurrentBatchParameters() const { return batch_parameters_; }

 protected:
  // Ingests IngestionVerificationPayload on record 0 and validates configuration.
  absl::Status OnConfig(const IngestionVerificationPayload& ingestion_verification_payload,
                        DoContext& context) override;

  // Ingests incoming batch messages (records 1..N), validating invariants and establishing batch
  // parameters.
  absl::Status OnRecord(const ShuffledMessage& message, DoContext& context) override;

  // Finalizes the replica session: validates batch size, cryptographically shuffles,
  // assigns group IDs, increments shuffle_count, and emits re-encrypted records.
  absl::Status OnFinalize(google::protobuf::Any config, FnContext& context) override;

 private:
  using FnContext = confidential_federated_compute::fns::Fn::FnContext;
  using DoContext = confidential_federated_compute::fns::DoFn::DoContext;

  std::optional<VerifiedIngestionVerificationResult> verified_result_;
  std::optional<BatchParameters> batch_parameters_;
  std::optional<MetricEpochKey> expected_metric_epoch_key_;
  std::vector<ShuffledMessage> messages_;

  IntegrityFaultHandler<ScatterShuffleHeader> fault_handler_;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_SCATTER_SHUFFLE_SCATTER_SHUFFLE_FN_H_
