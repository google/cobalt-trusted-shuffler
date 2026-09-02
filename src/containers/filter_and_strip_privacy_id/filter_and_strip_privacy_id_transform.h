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

#ifndef SRC_CONTAINERS_FILTER_AND_STRIP_PRIVACY_ID_FILTER_AND_STRIP_PRIVACY_ID_TRANSFORM_H_
#define SRC_CONTAINERS_FILTER_AND_STRIP_PRIVACY_ID_FILTER_AND_STRIP_PRIVACY_ID_TRANSFORM_H_

#include <memory>
#include <utility>

#include "absl/status/statusor.h"
#include "cc/crypto/encryption_key.h"
#include "cc/crypto/signing_key.h"
#include "containers/fns/confidential_transform_server.h"
#include "containers/fns/fn_factory.h"
#include "src/containers/filter_and_strip_privacy_id/filter_and_strip_privacy_id_fn.h"
#include "src/proto/config_constraints.pb.h"

namespace cobalt::trusted_shuffler {

class FilterAndStripPrivacyIdFnFactory : public confidential_federated_compute::fns::FnFactory {
 public:
  FilterAndStripPrivacyIdFnFactory() = default;
  explicit FilterAndStripPrivacyIdFnFactory(TrustedShufflerConfigConstraints config_constraints)
      : config_constraints_(std::move(config_constraints)) {}

  ~FilterAndStripPrivacyIdFnFactory() override = default;

  absl::StatusOr<std::unique_ptr<confidential_federated_compute::fns::Fn>> CreateFn()
      const override;

  const TrustedShufflerConfigConstraints& ConfigConstraints() const { return config_constraints_; }

 private:
  TrustedShufflerConfigConstraints config_constraints_;
};

// Factory function for creating a FilterAndStripPrivacyId transform service.
std::unique_ptr<confidential_federated_compute::fns::FnConfidentialTransform>
CreateFilterAndStripPrivacyIdTransform(
    std::unique_ptr<oak::crypto::SigningKeyHandle> signing_key_handle,
    std::unique_ptr<oak::crypto::EncryptionKeyHandle> encryption_key_handle);

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_FILTER_AND_STRIP_PRIVACY_ID_FILTER_AND_STRIP_PRIVACY_ID_TRANSFORM_H_
