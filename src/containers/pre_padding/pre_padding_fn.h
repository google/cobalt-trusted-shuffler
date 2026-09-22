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

#ifndef SRC_CONTAINERS_PRE_PADDING_PRE_PADDING_FN_H_
#define SRC_CONTAINERS_PRE_PADDING_PRE_PADDING_FN_H_

#include <optional>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "containers/fns/do_fn.h"
#include "google/protobuf/any.pb.h"
#include "src/containers/base/first_record_config_do_fn.h"
#include "src/containers/base/pipeline_nodes.h"
#include "src/containers/base/with_outputs.h"
#include "src/crypto/verify_ingestion_verification_payload.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"

namespace cobalt::trusted_shuffler {

// Generates encrypted dummy padding messages for approved partitions so each
// partition's total message count is an exact multiple of batch_num.
class PrePaddingFn
    : public FirstRecordConfigDoFn<IngestionVerificationPayload, ShuffledMessage>::WithOutputs<
          EncryptsTo<Node::kPaddingMessages>,
          EncryptedKV<MetricEpochKey, ShuffledMessage, Node::kPaddingMessages>,
          Unencrypted<IntegrityFault>> {
 public:
  PrePaddingFn() = default;
  ~PrePaddingFn() override = default;

  absl::Status FinalizeReplica(google::protobuf::Any config, FnContext& context) override;

 protected:
  absl::Status OnConfig(const IngestionVerificationPayload& config, DoContext& context) override;
  absl::Status OnRecord(const ShuffledMessage& input, DoContext& context) override;

 private:
  absl::Status SignalIntegrityFault(absl::string_view reason, FnContext& context);

  // Verified run context, approved partitions, and canonical ingestion fingerprint
  // unpacked from Record 0.
  std::optional<VerifiedIngestionVerificationResult> verified_result_;

  // Indicates whether an integrity fault occurred during the replica lifecycle.
  bool has_integrity_fault_ = false;

  // Ensures at most one unencrypted IntegrityFault record is emitted per replica.
  bool fault_emitted_ = false;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_PRE_PADDING_PRE_PADDING_FN_H_
