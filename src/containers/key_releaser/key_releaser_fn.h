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

#ifndef SRC_CONTAINERS_KEY_RELEASER_KEY_RELEASER_FN_H_
#define SRC_CONTAINERS_KEY_RELEASER_KEY_RELEASER_FN_H_

#include <optional>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "containers/fns/do_fn.h"
#include "google/protobuf/any.pb.h"
#include "src/containers/base/first_record_config_do_fn.h"
#include "src/crypto/multiset_fingerprint.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/release.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/util/metric_epoch_key_utils.h"

namespace cobalt::trusted_shuffler {

// DoFn implementing fingerprint verification and decryption key release.
//
// Receives an encrypted IngestionVerificationPayload on Record 0 containing the
// SignedRunContext and canonical ingestion fingerprint, merges PartialReleaseReport
// messages across all encrypt_output workers on Records 1..N, and verifies on
// FinalizeReplica that the complete release multiset fingerprint matches the
// canonical ingestion fingerprint before releasing the Tink AEAD release_key.
class KeyReleaserFn
    : public FirstRecordConfigDoFn<IngestionVerificationPayload, PartialReleaseReport> {
 public:
  KeyReleaserFn() = default;
  ~KeyReleaserFn() override = default;

  absl::Status FinalizeReplica(google::protobuf::Any config, FnContext& context) override;

  bool HasIntegrityFault() const { return has_integrity_fault_; }

  bool IsFinalized() const { return is_finalized_; }

  const std::optional<cobalt::trusted_shuffler::RunContext>& RunContext() const {
    return run_context_;
  }

  const std::optional<RunFingerprint>& CanonicalFingerprint() const {
    return canonical_fingerprint_;
  }

 protected:
  absl::Status OnConfig(const IngestionVerificationPayload& config, DoContext& context) override;

  absl::Status OnRecord(const PartialReleaseReport& report, DoContext& context) override;

 private:
  absl::Status EmitIntegrityFault(absl::string_view reason, FnContext& context);

  bool has_integrity_fault_ = false;
  bool has_emitted_output_ = false;
  bool is_finalized_ = false;
  std::optional<cobalt::trusted_shuffler::RunContext> run_context_;
  absl::flat_hash_set<MetricEpochKey> approved_partitions_;
  std::optional<RunFingerprint> canonical_fingerprint_;
  std::optional<MultisetFingerprint> multiset_fingerprint_;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_KEY_RELEASER_KEY_RELEASER_FN_H_
