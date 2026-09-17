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

#ifndef SRC_CONTAINERS_ENCRYPT_OUTPUT_ENCRYPT_OUTPUT_FN_H_
#define SRC_CONTAINERS_ENCRYPT_OUTPUT_ENCRYPT_OUTPUT_FN_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "containers/fns/do_fn.h"
#include "google/protobuf/any.pb.h"
#include "src/containers/base/first_record_config_do_fn.h"
#include "src/crypto/multiset_fingerprint.h"
#include "src/crypto/release_encrypter.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/release.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler {

// DoFn implementation for output encryption and release fingerprinting in the
// Trusted Shuffler pipeline.
//
// Inherits from FirstRecordConfigDoFn<IngestionVerificationPayload, ShuffledMessage>.
// - Record 0: Ingests IngestionVerificationPayload containing
//   IngestionVerificationResult (with SignedRunContext and ThresholdReport).
//   Verifies SignedRunContext via VerifySignedRunContext, validates
//   ThresholdReport run_id and approved partitions, initializes
//   ReleaseEncrypter from RunContext.release_key, and initializes the partial
//   release multiset fingerprint.
// - Records 1..N: Validates two-pass shuffled messages (shuffle_count == 2,
//   run_id match, metric_epoch_key in approved_partitions), accumulates release
//   fingerprint, encrypts customer messages for release using ReleaseEncrypter
//   (backed by RunContext.release_key), and emits unencrypted
//   EncryptedMessageOutput records.
// - Finalize: Emits PartialReleaseReport encrypted for the downstream key
//   release verification stage.
class EncryptOutputFn
    : public FirstRecordConfigDoFn<IngestionVerificationPayload, ShuffledMessage>::WithOutputs<
          UnencryptedKV<ReleaseOutputHeader, EncryptedMessageOutput>,
          UnencryptedKV<ReleaseOutputHeader, IntegrityFault>,
          EncryptedKV<ReleaseOutputHeader, PartialReleaseReport, 0>> {
 public:
  EncryptOutputFn() = default;
  ~EncryptOutputFn() override = default;

  // Finalizes the replica by emitting PartialReleaseReport encrypted for the
  // downstream key release verification stage.
  absl::Status FinalizeReplica(google::protobuf::Any config, FnContext& context) override;

 protected:
  // Hook invoked for Record 0 containing IngestionVerificationPayload.
  absl::Status OnConfig(const IngestionVerificationPayload& ingestion_verification_payload,
                        DoContext& context) override;

  // Hook invoked for all subsequent records (records 1..N).
  absl::Status OnRecord(const ShuffledMessage& message, DoContext& context) override;

 private:
  using FnContext = confidential_federated_compute::fns::Fn::FnContext;
  using DoContext = confidential_federated_compute::fns::DoFn::DoContext;

  absl::Status SignalIntegrityFault(FnContext& context);
  absl::Status SignalIntegrityFault(absl::string_view reason, FnContext& context);
  absl::Status EmitIntegrityFault(absl::string_view message, FnContext& context);

  bool is_finalized_ = false;
  std::optional<ReleaseEncrypter> release_encrypter_;
  std::string run_id_;
  std::optional<MultisetFingerprint> fingerprint_;
  absl::flat_hash_set<MetricEpochKey> approved_partitions_;
  bool has_integrity_fault_ = false;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_ENCRYPT_OUTPUT_ENCRYPT_OUTPUT_FN_H_
