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

#include "src/containers/ingestion_verification/ingestion_verification_transform.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "containers/fns/confidential_transform_server.h"
#include "containers/fns/fn_factory.h"
#include "google/protobuf/any.pb.h"
#include "src/containers/ingestion_verification/ingestion_verification_fn.h"
#include "src/proto/config_constraints.pb.h"

namespace cobalt::trusted_shuffler {
namespace {

using confidential_federated_compute::fns::Fn;
using confidential_federated_compute::fns::FnConfidentialTransform;
using confidential_federated_compute::fns::FnFactory;
using confidential_federated_compute::fns::WriteConfigurationMap;

}  // namespace

absl::StatusOr<std::unique_ptr<Fn>> IngestionVerificationFnFactory::CreateFn() const {
  return std::make_unique<IngestionVerificationFn>(config_constraints_);
}

absl::StatusOr<std::unique_ptr<FnFactory>> ProvideIngestionVerificationFnFactory(
    const google::protobuf::Any& configuration, const google::protobuf::Any& config_constraints,
    const WriteConfigurationMap& write_configuration_map) {
  TrustedShufflerConfigConstraints constraints;
  if (!config_constraints.UnpackTo(&constraints)) {
    return absl::InvalidArgumentError(
        "Failed to unpack config_constraints as "
        "TrustedShufflerConfigConstraints.");
  }

  absl::flat_hash_set<std::pair<int32_t, int32_t>> seen_metrics;
  for (const auto& metric_threshold : constraints.metric_thresholds()) {
    if (metric_threshold.project_id() <= 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Project ID must be positive, got ", metric_threshold.project_id()));
    }
    if (metric_threshold.metric_id() <= 0) {
      return absl::InvalidArgumentError(
          absl::StrCat("Metric ID must be positive, got ", metric_threshold.metric_id()));
    }
    if (metric_threshold.threshold() <= 0) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Threshold must be positive, got ", metric_threshold.threshold(), " for project ",
          metric_threshold.project_id(), ", metric ", metric_threshold.metric_id()));
    }
    if (!seen_metrics.insert({metric_threshold.project_id(), metric_threshold.metric_id()})
             .second) {
      return absl::InvalidArgumentError(absl::StrCat("Duplicate metric threshold for project ",
                                                     metric_threshold.project_id(), " and metric ",
                                                     metric_threshold.metric_id()));
    }
  }

  return std::make_unique<IngestionVerificationFnFactory>(std::move(constraints));
}

std::unique_ptr<FnConfidentialTransform> CreateIngestionVerificationTransform(
    std::unique_ptr<oak::crypto::SigningKeyHandle> signing_key_handle,
    std::unique_ptr<oak::crypto::EncryptionKeyHandle> encryption_key_handle) {
  return std::make_unique<FnConfidentialTransform>(std::move(signing_key_handle),
                                                   ProvideIngestionVerificationFnFactory,
                                                   std::move(encryption_key_handle));
}

}  // namespace cobalt::trusted_shuffler
