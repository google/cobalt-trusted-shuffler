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

#ifndef SRC_CRYPTO_MULTISET_FINGERPRINT_H_
#define SRC_CRYPTO_MULTISET_FINGERPRINT_H_

#include <cstdint>
#include <map>
#include <string>
#include <tuple>

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/run_context.pb.h"

namespace cobalt::trusted_shuffler {

// MultisetFingerprint tracks and aggregates message counts per MetricEpochKey across
// processing pipeline stages, and validates partial and complete RunFingerprints.
// TODO(b/555363748): Implement actual cryptographic multiset fingerprinting algorithm (bytes are currently empty).
class MultisetFingerprint {
 public:
  MultisetFingerprint() = default;
  MultisetFingerprint(Stage stage_id, bool is_partial, absl::string_view run_id);
  MultisetFingerprint(Stage stage_id, bool is_partial, const RunContext& run_context);

  // Adds a message to the fingerprint. If message.is_padding == true, no-op.
  void AddMessage(const ShuffledMessage& message);

  // Merges another partial RunFingerprint into this complete fingerprint.
  // Returns an error if this fingerprint is partial, if partial_fingerprint is for a
  // different stage, or if partial_fingerprint is not partial.
  absl::Status Merge(const RunFingerprint& partial_fingerprint);

  // Builds and returns the current RunFingerprint with message counts.
  RunFingerprint Build() const;

  // Verifies this fingerprint against a complete (non-partial) RunFingerprint.
  // Returns false if complete_fingerprint is for a different run ID, if either
  // fingerprint is partial, or if message counts do not match.
  bool VerifyComplete(const RunFingerprint& complete_fingerprint) const;

  // Verifies a metric epoch key fingerprint entry and its message count against a
  // complete (non-partial) RunFingerprint.
  // Returns false if complete_fingerprint is for a different run ID, if either
  // fingerprint is partial, if this MultisetFingerprint contains partitions other
  // than metric_epoch_key, or if message count does not match.
  bool VerifyCompleteForMetricEpoch(const RunFingerprint& complete_fingerprint,
                                   const MetricEpochKey& metric_epoch_key) const;

 private:
  using MetricEpochTuple = std::tuple<int32_t, int32_t, int64_t>;

  Stage stage_id_ = STAGE_UNSPECIFIED;
  bool is_partial_ = false;
  std::string run_id_;
  std::map<MetricEpochTuple, int64_t> partition_counts_;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CRYPTO_MULTISET_FINGERPRINT_H_
