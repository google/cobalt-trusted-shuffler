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

#ifndef SRC_CONTAINERS_GATHER_SHUFFLE_GATHER_SHUFFLE_FN_H_
#define SRC_CONTAINERS_GATHER_SHUFFLE_GATHER_SHUFFLE_FN_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/any.pb.h"
#include "src/containers/base/first_record_config_do_fn.h"
#include "src/containers/base/integrity_fault.h"
#include "src/containers/base/pipeline_nodes.h"
#include "src/crypto/verify_ingestion_verification_payload.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"

namespace cobalt::trusted_shuffler {

// Gathers scattered ShuffledMessage records sharing a (MetricEpochKey, group_id)
// pair, verifies expected group size, performs the second cryptographic shuffle,
// and emits re-encrypted messages with shuffle_count = 2.
//
// A replica must only be given a gather group that holds at least one message.
// The group's identity travels in the messages themselves, so a replica that
// receives none cannot tell which group it was meant to serve nor how many
// messages that group owes, and reports an integrity fault rather than
// completing a group it cannot account for. Gather groups that hold no messages
// exist whenever batch_num exceeds actual_batch_size (see b/564506496); the
// untrusted orchestrator routes by observed messages and so never raises a
// replica for one.
class GatherShuffleFn
    : public FirstRecordConfigDoFn<IngestionVerificationPayload, ShuffledMessage>::WithOutputs<
          EncryptsTo<Node::kGatheredMessages>, Encrypted<ShuffledMessage, Node::kGatheredMessages>,
          Unencrypted<IntegrityFault>> {
 public:
  GatherShuffleFn() = default;
  ~GatherShuffleFn() override = default;

  absl::Status FinalizeReplica(google::protobuf::Any config, FnContext& context) override;

  bool HasIntegrityFault() const { return fault_handler_.HasFault(); }

 protected:
  absl::Status OnConfig(const IngestionVerificationPayload& config, DoContext& context) override;
  absl::Status OnRecord(const ShuffledMessage& input, DoContext& context) override;

 private:
  IntegrityFaultHandler<NoKey> fault_handler_;

  // Verified run context, approved partitions, and canonical ingestion fingerprint
  // unpacked from Record 0.
  std::optional<VerifiedIngestionVerificationResult> verified_result_;

  // Partition key established by the first message in the gather group.
  std::optional<MetricEpochKey> expected_metric_epoch_key_;

  // Gather group index established by the first message in the gather group.
  std::optional<int32_t> expected_group_id_;

  // Expected number of messages for this (partition, group_id) gather group.
  std::optional<int32_t> expected_group_size_;

  // Buffered messages accumulated for the second-pass cryptographic shuffle.
  std::vector<ShuffledMessage> messages_;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_GATHER_SHUFFLE_GATHER_SHUFFLE_FN_H_
