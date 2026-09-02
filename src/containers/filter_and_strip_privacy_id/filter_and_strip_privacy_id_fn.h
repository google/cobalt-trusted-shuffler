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

#ifndef SRC_CONTAINERS_FILTER_AND_STRIP_PRIVACY_ID_FILTER_AND_STRIP_PRIVACY_ID_FN_H_
#define SRC_CONTAINERS_FILTER_AND_STRIP_PRIVACY_ID_FILTER_AND_STRIP_PRIVACY_ID_FN_H_

#include <utility>

#include "absl/status/status.h"
#include "containers/fns/do_fn.h"
#include "src/proto/config_constraints.pb.h"

namespace cobalt::trusted_shuffler {

// A DoFn class for filter_and_strip_privacy_id.
// Skeleton implementation that does nothing yet.
class FilterAndStripPrivacyIdFn : public confidential_federated_compute::fns::DoFn {
 public:
  FilterAndStripPrivacyIdFn() = default;
  explicit FilterAndStripPrivacyIdFn(TrustedShufflerConfigConstraints config_constraints)
      : config_constraints_(std::move(config_constraints)) {}

  ~FilterAndStripPrivacyIdFn() override = default;

  // Processes incoming KV pairs. Skeleton implementation that does nothing yet.
  absl::Status Do(KV input, Context& context) override;

  const TrustedShufflerConfigConstraints& ConfigConstraints() const { return config_constraints_; }

 private:
  TrustedShufflerConfigConstraints config_constraints_;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_FILTER_AND_STRIP_PRIVACY_ID_FILTER_AND_STRIP_PRIVACY_ID_FN_H_
