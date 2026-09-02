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

#include "src/containers/ingestion_verification/ingestion_verification_fn.h"

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "google/protobuf/any.pb.h"

namespace cobalt::trusted_shuffler {

absl::Status IngestionVerificationFn::InitializeReplica(google::protobuf::Any config,
                                                        Context& context) {
  for (const auto& metric_threshold : config_constraints_.metric_thresholds()) {
    LOG(INFO) << "Config constraint: project_id=" << metric_threshold.project_id()
              << ", metric_id=" << metric_threshold.metric_id()
              << ", threshold=" << metric_threshold.threshold();
  }
  return absl::OkStatus();
}

absl::Status IngestionVerificationFn::Do(KV input, Context& context) {
  // Currently this container does nothing and emits no outputs.
  // TODO(b/555370722): Merge partial fingerprints evaluate device counts
  // against reporting thresholds.
  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler
