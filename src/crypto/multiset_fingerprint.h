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

#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/run_context.pb.h"

namespace cobalt::trusted_shuffler {

// MultisetFingerprint tracks and aggregates message counts and computes
// cryptographic multiset fingerprints per MetricEpochKey across processing
// pipeline stages using MSet-Add-Hash (HMAC-SHA256 with addition modulo 2^256),
// and validates partial and complete RunFingerprints.
class MultisetFingerprint {
 public:
  static constexpr std::size_t kFingerprintSize = 32;

  MultisetFingerprint(Stage stage_id, bool is_partial, const RunContext& run_context,
                      std::size_t fingerprint_size);

  // Returns the configured fingerprint size in bytes.
  std::size_t FingerprintSize() const { return fingerprint_size_; }

  // Adds a message to the fingerprint. If message.is_padding == true, no-op.
  void AddMessage(const ShuffledMessage& message);

  // Merges another partial RunFingerprint into this complete fingerprint.
  // Returns an error if this fingerprint is partial, if partial_fingerprint is for a
  // different stage or different run ID, or if partial_fingerprint is not partial.
  absl::Status Merge(const RunFingerprint& partial_fingerprint);

  // Builds and returns the current RunFingerprint with message counts and multiset fingerprints.
  RunFingerprint Build() const;

  // Verifies this fingerprint against a complete (non-partial) RunFingerprint.
  // Returns false if complete_fingerprint is for a different run ID, if either
  // fingerprint is partial, or if message counts or multiset fingerprints do not match.
  bool VerifyComplete(const RunFingerprint& complete_fingerprint) const;

  // Verifies a metric epoch key fingerprint entry and its message count against a
  // complete (non-partial) RunFingerprint.
  // Returns false if complete_fingerprint is for a different run ID, if either
  // fingerprint is partial, if this MultisetFingerprint contains partitions other
  // than metric_epoch_key, or if message count or multiset fingerprint does not match.
  bool VerifyCompleteForMetricEpoch(const RunFingerprint& complete_fingerprint,
                                    const MetricEpochKey& metric_epoch_key) const;

 private:
  using MetricEpochTuple = std::tuple<int32_t, int32_t, int64_t>;

  struct PartitionValue {
    int64_t count = 0;
    std::string fingerprint;

    bool operator==(const PartitionValue& other) const = default;
  };

  Stage stage_id_ = STAGE_UNSPECIFIED;
  bool is_partial_ = false;
  std::string run_id_;
  std::string run_hash_key_;
  std::size_t fingerprint_size_;
  absl::flat_hash_map<MetricEpochTuple, PartitionValue> partitions_;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CRYPTO_MULTISET_FINGERPRINT_H_
