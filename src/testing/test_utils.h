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
#include "absl/types/span.h"
#include "src/crypto/verify_signed_run_context.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/proto/trusted_shuffler_upload.pb.h"
#include "tensorflow_federated/cc/core/impl/aggregation/core/mutable_vector_data.h"
#include "tensorflow_federated/cc/core/impl/aggregation/core/tensor.h"
#include "tensorflow_federated/cc/core/impl/aggregation/protocol/checkpoint_builder.h"
#include "tensorflow_federated/cc/core/impl/aggregation/protocol/federated_compute_checkpoint_builder.h"

namespace cobalt::trusted_shuffler::testing {

inline constexpr absl::string_view kTestRunId = "0123456789abcdef0123456789abcdef";
inline constexpr absl::string_view kTestRunHashKey = "fedcba9876543210fedcba9876543210";

struct MetricEpochSpec {
  int32_t project_id;
  int32_t metric_id;
  int64_t epoch_index;
};

struct MetricIdentifierSpec {
  int32_t project_id;
  int32_t metric_id;
};

// Builds a MetricEpochKey proto from declarative test specifications.
inline MetricEpochKey CreateMetricEpochKey(const MetricEpochSpec& spec) {
  MetricEpochKey key;
  key.set_project_id(spec.project_id);
  key.set_metric_id(spec.metric_id);
  key.set_epoch_index(spec.epoch_index);
  return key;
}

// Builds a valid RunContext proto with specified metric identifiers and epoch bounds.
inline RunContext CreateValidRunContext(absl::Span<const MetricIdentifierSpec> identifiers,
                                        int64_t min_epoch, int64_t max_epoch, int32_t batch_size) {
  RunContext run_context;
  run_context.set_run_id(std::string(kTestRunId));
  run_context.set_run_hash_key(std::string(kTestRunHashKey));
  auto* run_config = run_context.mutable_run_config();
  for (const auto& id : identifiers) {
    auto* metric = run_config->add_metric_identifiers();
    metric->set_project_id(id.project_id);
    metric->set_metric_id(id.metric_id);
  }
  run_config->set_min_epoch_index(min_epoch);
  run_config->set_max_epoch_index(max_epoch);
  run_config->set_target_batch_size(batch_size);
  return run_context;
}

// Builds a SignedRunContext wrapping serialized RunContext with a dummy valid signature.
inline SignedRunContext CreateValidSignedRunContext(const RunContext& run_context) {
  SignedRunContext signed_run_context;
  signed_run_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_run_context.set_signature("valid_dummy_signature");
  return signed_run_context;
}

// Builds a ShuffledMessage proto with the specified customer payload.
inline ShuffledMessage CreateShuffledMessage(absl::string_view payload) {
  ShuffledMessage msg;
  msg.set_customer_message(std::string(payload));
  return msg;
}

// Builds an IngestionVerificationPayload containing a ThresholdReport with approved partitions
// and the signed run context.
inline IngestionVerificationPayload CreateThresholdReportPayload(
    absl::string_view run_id, absl::Span<const MetricEpochSpec> approved_specs,
    const SignedRunContext& signed_run_context) {
  IngestionVerificationPayload payload;
  auto* success = payload.mutable_success();
  auto* report = success->mutable_threshold_report();
  report->set_run_id(std::string(run_id));
  for (const auto& spec : approved_specs) {
    auto* partition = report->add_approved_partitions();
    partition->set_project_id(spec.project_id);
    partition->set_metric_id(spec.metric_id);
    partition->set_epoch_index(spec.epoch_index);
  }
  *success->mutable_signed_run_context() = signed_run_context;
  return payload;
}

// Builds an IngestionVerificationPayload containing an IntegrityFault.
inline IngestionVerificationPayload CreateIntegrityFaultPayload() {
  IngestionVerificationPayload payload;
  payload.mutable_integrity_fault();
  return payload;
}

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
  run_context.set_run_hash_key(std::string(kRunHashKeyByteLength, 'b'));
  auto* run_config = run_context.mutable_run_config();
  RunConfig::MetricIdentifier* metric = run_config->add_metric_identifiers();
  metric->set_project_id(project_id);
  metric->set_metric_id(metric_id);
  run_config->set_min_epoch_index(min_epoch_index);
  run_config->set_max_epoch_index(max_epoch_index);
  run_config->set_target_batch_size(100);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature("test_signature");
  return signed_context;
}

}  // namespace cobalt::trusted_shuffler::testing

#endif  // SRC_TESTING_TEST_UTILS_H_
