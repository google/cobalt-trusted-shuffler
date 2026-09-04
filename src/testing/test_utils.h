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

#ifndef SRC_TESTING_TEST_UTILS_H_
#define SRC_TESTING_TEST_UTILS_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/string_view.h"
#include "src/proto/run_context.pb.h"
#include "src/proto/trusted_shuffler_upload.pb.h"
#include "tensorflow_federated/cc/core/impl/aggregation/core/mutable_vector_data.h"
#include "tensorflow_federated/cc/core/impl/aggregation/core/tensor.h"
#include "tensorflow_federated/cc/core/impl/aggregation/protocol/checkpoint_builder.h"
#include "tensorflow_federated/cc/core/impl/aggregation/protocol/federated_compute_checkpoint_builder.h"

namespace cobalt::trusted_shuffler::testing {

// Builds an FCKP checkpoint tensor string from uploads, event times, and an optional privacy id.
// Callers must provide all arguments explicitly without default arguments.
inline std::string CreateTestFcCheckpoint(const std::vector<TrustedShufflerUpload>& uploads,
                                          const std::vector<std::string>& event_times,
                                          const std::optional<std::string>& privacy_id) {
  tensorflow_federated::aggregation::FederatedComputeCheckpointBuilderFactory builder_factory;
  std::unique_ptr<tensorflow_federated::aggregation::CheckpointBuilder> builder =
      builder_factory.Create();

  std::vector<std::string> entries;
  entries.reserve(uploads.size());
  for (const TrustedShufflerUpload& upload : uploads) {
    entries.push_back(upload.SerializeAsString());
  }

  std::vector<absl::string_view> entry_views(entries.begin(), entries.end());
  auto entry_data =
      std::make_unique<tensorflow_federated::aggregation::MutableVectorData<absl::string_view>>(
          std::move(entry_views));
  auto entry_tensor = tensorflow_federated::aggregation::Tensor::Create(
      tensorflow_federated::aggregation::DT_STRING,
      tensorflow_federated::aggregation::TensorShape({static_cast<int64_t>(entries.size())}),
      std::move(entry_data));
  CHECK_OK(entry_tensor.status());
  CHECK_OK(builder->Add("upload_query/entry", *entry_tensor));

  if (!event_times.empty()) {
    std::vector<absl::string_view> time_views(event_times.begin(), event_times.end());
    const int64_t time_count = static_cast<int64_t>(time_views.size());
    auto time_data =
        std::make_unique<tensorflow_federated::aggregation::MutableVectorData<absl::string_view>>(
            std::move(time_views));
    auto time_tensor = tensorflow_federated::aggregation::Tensor::Create(
        tensorflow_federated::aggregation::DT_STRING,
        tensorflow_federated::aggregation::TensorShape({time_count}), std::move(time_data));
    CHECK_OK(time_tensor.status());
    CHECK_OK(builder->Add("upload_query/confidential_compute_event_time", *time_tensor));
  }

  if (privacy_id.has_value()) {
    std::vector<absl::string_view> privacy_views = {*privacy_id};
    auto privacy_data =
        std::make_unique<tensorflow_federated::aggregation::MutableVectorData<absl::string_view>>(
            std::move(privacy_views));
    auto privacy_tensor = tensorflow_federated::aggregation::Tensor::Create(
        tensorflow_federated::aggregation::DT_STRING,
        tensorflow_federated::aggregation::TensorShape({1}), std::move(privacy_data));
    CHECK_OK(privacy_tensor.status());
    CHECK_OK(builder->Add("confidential_compute_privacy_id", *privacy_tensor));
  }

  auto checkpoint = builder->Build();
  CHECK_OK(checkpoint.status());
  return std::string(*checkpoint);
}

// Builds a SignedRunContext with specified project_id, metric_id, run_id, and epoch bounds.
inline SignedRunContext CreateValidSignedRunContext(const int32_t project_id,
                                                    const int32_t metric_id,
                                                    const absl::string_view run_id,
                                                    const int64_t min_epoch_index,
                                                    const int64_t max_epoch_index) {
  RunContext run_context;
  run_context.set_run_id(std::string(run_id));
  auto* run_config = run_context.mutable_run_config();
  RunConfig::MetricIdentifier* metric = run_config->add_metric_identifiers();
  metric->set_project_id(project_id);
  metric->set_metric_id(metric_id);
  run_config->set_min_epoch_index(min_epoch_index);
  run_config->set_max_epoch_index(max_epoch_index);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  return signed_context;
}

}  // namespace cobalt::trusted_shuffler::testing

#endif  // SRC_TESTING_TEST_UTILS_H_
