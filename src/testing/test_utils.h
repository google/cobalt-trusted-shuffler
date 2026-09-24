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
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "cc/crypto/signing_key.h"
#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "proto/attestation/eventlog.pb.h"
#include "proto/attestation/verification.pb.h"
#include "proto/session/messages.pb.h"
#include "src/crypto/release_encrypter.h"
#include "src/crypto/verify_signed_run_context.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/proto/trusted_shuffler_upload.pb.h"
#include "tensorflow_federated/cc/core/impl/aggregation/core/mutable_vector_data.h"
#include "tensorflow_federated/cc/core/impl/aggregation/core/tensor.h"
#include "tensorflow_federated/cc/core/impl/aggregation/protocol/checkpoint_builder.h"
#include "tensorflow_federated/cc/core/impl/aggregation/protocol/federated_compute_checkpoint_builder.h"
#include "tink/cleartext_keyset_handle.h"
#include "tink/config/global_registry.h"
#include "tink/keyset_handle.h"
#include "tink/public_key_sign.h"
#include "tink/signature/signature_config.h"
#include "tink/signature/signature_key_templates.h"

namespace cobalt::trusted_shuffler::testing {

inline constexpr absl::string_view kTestRunId = "0123456789abcdef0123456789abcdef";
inline constexpr absl::string_view kTestRunHashKey = "fedcba9876543210fedcba9876543210";

// Returns a 32-byte run_id padded with trailing zeros or unchanged if empty.
inline std::string PadRunId(absl::string_view prefix) {
  if (prefix.empty()) {
    return "";
  }
  std::string padded(prefix);
  if (padded.size() < kRunIdByteLength) {
    padded.resize(kRunIdByteLength, '0');
  }
  return padded;
}

class TestSigningKey {
 public:
  TestSigningKey() {
    CHECK_OK(crypto::tink::SignatureConfig::Register());
    auto keyset_handle =
        crypto::tink::KeysetHandle::GenerateNew(crypto::tink::SignatureKeyTemplates::EcdsaP256Raw(),
                                                crypto::tink::KeyGenConfigGlobalRegistry());
    CHECK_OK(keyset_handle.status());
    keyset_handle_ = std::move(*keyset_handle);

    auto signer = keyset_handle_->GetPrimitive<crypto::tink::PublicKeySign>(
        crypto::tink::ConfigGlobalRegistry());
    CHECK_OK(signer.status());
    signer_ = std::move(*signer);

    auto public_handle =
        keyset_handle_->GetPublicKeysetHandle(crypto::tink::KeyGenConfigGlobalRegistry());
    CHECK_OK(public_handle.status());

    auto keyset = crypto::tink::CleartextKeysetHandle::GetKeyset(**public_handle);
    const std::string& key_data = keyset.key(0).key_data().value();
    std::string x_bytes;
    std::string y_bytes;
    google::protobuf::io::ArrayInputStream array_stream(key_data.data(), key_data.size());
    google::protobuf::io::CodedInputStream input(&array_stream);
    uint32_t tag = 0;
    while ((tag = input.ReadTag()) != 0) {
      int field_number = tag >> 3;
      int wire_type = tag & 0x7;
      if (wire_type == 2) {
        uint32_t length = 0;
        CHECK(input.ReadVarint32(&length));
        std::string bytes;
        CHECK(input.ReadString(&bytes, length));
        auto normalize_coordinate = [](std::string b) -> std::string {
          if (b.size() == 33 && b[0] == '\0') {
            return b.substr(1);
          }
          if (b.size() < 32) {
            return absl::StrCat(std::string(32 - b.size(), '\0'), b);
          }
          if (b.size() > 32) {
            return b.substr(b.size() - 32);
          }
          return b;
        };
        if (field_number == 3) {
          x_bytes = normalize_coordinate(std::move(bytes));
        } else if (field_number == 4) {
          y_bytes = normalize_coordinate(std::move(bytes));
        }
      } else if (wire_type == 0) {
        uint64_t dummy = 0;
        CHECK(input.ReadVarint64(&dummy));
      } else if (wire_type == 1) {
        CHECK(input.Skip(8));
      } else if (wire_type == 5) {
        CHECK(input.Skip(4));
      }
    }
    CHECK_EQ(x_bytes.size(), 32);
    CHECK_EQ(y_bytes.size(), 32);
    public_key_bytes_ = absl::StrCat(x_bytes, y_bytes);

    oak::attestation::v1::ContainerLayerData container_layer_data;
    container_layer_data.set_signing_public_key(UncompressedPublicKeyBytes());
    google::protobuf::Any any;
    any.PackFrom(container_layer_data);
    oak::attestation::v1::Event event;
    event.set_tag("ORCHESTRATOR");
    *event.mutable_event() = any;

    oak::attestation::v1::Event stage0_event;
    stage0_event.set_tag("Stage0");
    oak::attestation::v1::Event system_event;
    system_event.set_tag("stage1");

    endorsed_evidence_.mutable_evidence()->mutable_event_log()->add_encoded_events(
        stage0_event.SerializeAsString());
    endorsed_evidence_.mutable_evidence()->mutable_event_log()->add_encoded_events(
        system_event.SerializeAsString());
    endorsed_evidence_.mutable_evidence()->mutable_event_log()->add_encoded_events(
        event.SerializeAsString());
  }

  std::string Sign(absl::string_view data) const {
    auto signature = signer_->Sign(data);
    CHECK_OK(signature.status());
    return *signature;
  }

  const std::string& PublicKeyBytes() const { return public_key_bytes_; }
  const oak::session::v1::EndorsedEvidence& EndorsedEvidence() const { return endorsed_evidence_; }

  // Backwards compatibility forwarders.
  const std::string& public_key_bytes() const { return PublicKeyBytes(); }
  const oak::session::v1::EndorsedEvidence& endorsed_evidence() const { return EndorsedEvidence(); }

  std::string UncompressedPublicKeyBytes() const { return absl::StrCat("\x04", public_key_bytes_); }

  oak::session::v1::EndorsedEvidence CreateEndorsedEvidenceWithContainerLayerData() const {
    return endorsed_evidence_;
  }

 private:
  std::unique_ptr<crypto::tink::KeysetHandle> keyset_handle_;
  std::unique_ptr<crypto::tink::PublicKeySign> signer_;
  std::string public_key_bytes_;
  oak::session::v1::EndorsedEvidence endorsed_evidence_;
};

class TestSigningKeyHandle : public oak::crypto::SigningKeyHandle {
 public:
  explicit TestSigningKeyHandle(const TestSigningKey* key) : key_(key) {}

  absl::StatusOr<oak::crypto::v1::Signature> Sign(absl::string_view message) override {
    oak::crypto::v1::Signature signature_proto;
    signature_proto.set_signature(key_->Sign(message));
    return signature_proto;
  }

 private:
  const TestSigningKey* key_;
};

inline const TestSigningKey& GetDefaultTestSigningKey() {
  static const auto* const key = new TestSigningKey();
  return *key;
}

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
  absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  CHECK_OK(release_key.status());

  RunContext run_context;
  run_context.set_run_id(std::string(kTestRunId));
  run_context.set_run_hash_key(std::string(kTestRunHashKey));
  run_context.set_release_key(*release_key);
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

// Builds a SignedRunContext wrapping serialized RunContext with a valid signature and evidence.
inline SignedRunContext CreateValidSignedRunContext(const RunContext& run_context,
                                                    const TestSigningKey& signing_key) {
  SignedRunContext signed_run_context;
  signed_run_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_run_context.set_signature(signing_key.Sign(signed_run_context.serialized_run_context()));
  *signed_run_context.mutable_endorsed_evidence() = signing_key.EndorsedEvidence();
  return signed_run_context;
}

inline SignedRunContext CreateValidSignedRunContext(const RunContext& run_context) {
  return CreateValidSignedRunContext(run_context, GetDefaultTestSigningKey());
}

// Builds a ShuffledMessage proto with the specified customer payload.
inline ShuffledMessage CreateShuffledMessage(absl::string_view payload) {
  ShuffledMessage msg;
  msg.set_customer_message(std::string(payload));
  return msg;
}

// Specification struct for building a ShuffledMessage in tests.
struct ShuffledMessageSpec {
  absl::string_view payload;
  absl::string_view run_id;
  MetricEpochKey metric_epoch_key;
  int32_t shuffle_count;
};

struct FingerprintSpec {
  MetricEpochKey metric_epoch_key;
  int64_t message_count;
};

// Builds an IngestionVerificationPayload mimicking expected success state.
inline IngestionVerificationPayload CreateIngestionVerificationPayload(
    absl::string_view run_id, absl::Span<const MetricEpochKey> approved_partitions,
    absl::Span<const FingerprintSpec> canonical_fingerprint,
    const SignedRunContext& signed_run_context) {
  IngestionVerificationResult result;
  *result.mutable_signed_run_context() = signed_run_context;

  auto* threshold = result.mutable_threshold_report();
  threshold->set_run_id(std::string(run_id));
  for (const MetricEpochKey& partition : approved_partitions) {
    *threshold->add_approved_partitions() = partition;
  }

  auto* fp = result.mutable_canonical_fingerprint();
  fp->set_run_id(std::string(run_id));
  fp->set_stage_id(cobalt::trusted_shuffler::STAGE_INGESTION);
  fp->set_is_partial(false);
  for (const auto& spec : canonical_fingerprint) {
    auto* partition_fp = fp->add_entries();
    *partition_fp->mutable_metric_epoch_key() = spec.metric_epoch_key;
    partition_fp->set_message_count(spec.message_count);
    partition_fp->set_fingerprint(std::string(32, 'x'));
  }

  IngestionVerificationPayload payload;
  *payload.mutable_success() = result;
  return payload;
}

// Builds a ShuffledMessage proto with the specified parameters.
inline ShuffledMessage CreateShuffledMessage(const ShuffledMessageSpec& spec) {
  ShuffledMessage msg;
  msg.set_customer_message(std::string(spec.payload));
  msg.set_run_id(std::string(spec.run_id));
  *msg.mutable_metric_epoch_key() = spec.metric_epoch_key;
  msg.set_shuffle_count(spec.shuffle_count);
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
  const MetricIdentifierSpec identifier = {
      .project_id = project_id,
      .metric_id = metric_id,
  };
  RunContext run_context =
      CreateValidRunContext({identifier}, min_epoch_index, max_epoch_index, /*batch_size=*/100);
  run_context.set_run_id(std::string(run_id));
  return CreateValidSignedRunContext(run_context);
}

}  // namespace cobalt::trusted_shuffler::testing

#endif  // SRC_TESTING_TEST_UTILS_H_
