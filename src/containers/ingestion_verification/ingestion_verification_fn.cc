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

#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "google/protobuf/any.pb.h"
#include "src/proto/config_constraints.pb.h"
#include "src/proto/run_context.pb.h"

namespace cobalt::trusted_shuffler {

IngestionVerificationFn::IngestionVerificationFn(
    TrustedShufflerConfigConstraints config_constraints)
    : config_constraints_(std::move(config_constraints)) {}

absl::Status IngestionVerificationFn::InitializeReplica(google::protobuf::Any config,
                                                        Context& context) {
  for (const auto& metric_threshold : config_constraints_.metric_thresholds()) {
    LOG(INFO) << "Config constraint: project_id=" << metric_threshold.project_id()
              << ", metric_id=" << metric_threshold.metric_id()
              << ", threshold=" << metric_threshold.threshold();
  }
  return absl::OkStatus();
}

absl::Status IngestionVerificationFn::OnConfig(
    const cobalt::trusted_shuffler::SignedRunContext& signed_run_context, Context& context) {
  RunContext run_context;
  if (!run_context.ParseFromString(signed_run_context.serialized_run_context())) {
    return absl::InvalidArgumentError(
        "Failed to parse RunContext proto from serialized_run_context.");
  }
  LOG(INFO) << "SignedRunContext received:\n" << run_context.DebugString();
  return absl::OkStatus();
}

absl::Status IngestionVerificationFn::OnRecord(KV record, Context& context) {
  // Currently a no-op that does nothing and emits no outputs.
  // TODO(b/555370722): Implement ingestion verification logic for data records.
  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler
