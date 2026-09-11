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

#ifndef SRC_UTIL_METRIC_EPOCH_KEY_UTILS_H_
#define SRC_UTIL_METRIC_EPOCH_KEY_UTILS_H_

#include <utility>

#include "src/proto/common.pb.h"

namespace cobalt::trusted_shuffler {

// Hash and equality support for MetricEpochKey in Abseil flat_hash containers.
template <typename H>
H AbslHashValue(H h, const MetricEpochKey& key) {
  return H::combine(std::move(h), key.project_id(), key.metric_id(), key.epoch_index());
}

inline bool operator==(const MetricEpochKey& a, const MetricEpochKey& b) {
  return a.project_id() == b.project_id() && a.metric_id() == b.metric_id() &&
         a.epoch_index() == b.epoch_index();
}

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_UTIL_METRIC_EPOCH_KEY_UTILS_H_
