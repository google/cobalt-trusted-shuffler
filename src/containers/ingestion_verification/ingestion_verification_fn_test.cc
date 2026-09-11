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

#include <cstdint>
#include <string>
#include <utility>

#include "absl/log/absl_check.h"
#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "gtest/gtest.h"
#include "src/crypto/multiset_fingerprint.h"
#include "src/crypto/release_encrypter.h"
#include "src/crypto/verify_signed_run_context.h"
#include "src/proto/common.pb.h"
#include "src/proto/config_constraints.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/proto/sketch.pb.h"
#include "src/sketch/hll_sketch.h"
#include "src/testing/fake_context.h"
#include "src/testing/matchers.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cobalt::trusted_shuffler::testing::EquivToProto;
using ::cobalt::trusted_shuffler::testing::FakeContext;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;

constexpr absl::string_view kTestRunId = "test_run_id_32_bytes_abcdefghijk";
static_assert(kTestRunId.size() == kRunIdByteLength);

// Helper struct for declarative test fixtures without positional tuples.
struct MetricThresholdSpec {
  int32_t project_id;
  int32_t metric_id;
  int32_t threshold;
};

// Declarative helper requiring explicit arguments (no default parameters).
TrustedShufflerConfigConstraints CreateConfigConstraints(
    absl::Span<const MetricThresholdSpec> specs) {
  TrustedShufflerConfigConstraints constraints;
  for (const auto& spec : specs) {
    auto* const mt = constraints.add_metric_thresholds();
    mt->set_project_id(spec.project_id);
    mt->set_metric_id(spec.metric_id);
    mt->set_threshold(spec.threshold);
  }
  return constraints;
}

SignedRunContext CreateValidSignedRunContext(int32_t project_id, int32_t metric_id,
                                             std::string signature_value) {
  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ABSL_CHECK_OK(release_key.status());

  RunContext run_context;
  run_context.set_run_id(std::string(kTestRunId));
  run_context.set_run_hash_key(std::string(kRunHashKeyByteLength, 'b'));
  run_context.set_release_key(*release_key);

  RunConfig* const config = run_context.mutable_run_config();
  config->set_min_epoch_index(10);
  config->set_max_epoch_index(20);
  config->set_target_batch_size(100);

  RunConfig::MetricIdentifier* const metric = config->add_metric_identifiers();
  metric->set_project_id(project_id);
  metric->set_metric_id(metric_id);

  SignedRunContext signed_context;
  signed_context.set_serialized_run_context(run_context.SerializeAsString());
  signed_context.set_signature(std::move(signature_value));
  return signed_context;
}

struct PartitionSpec {
  int32_t project_id;
  int32_t metric_id;
  int64_t epoch_index;
  int64_t message_count;
  std::vector<std::string> device_ids;
  int32_t sketch_precision;
};

MetricEpochKey CreateMetricEpochKey(int32_t project_id, int32_t metric_id, int64_t epoch_index) {
  MetricEpochKey key;
  key.set_project_id(project_id);
  key.set_metric_id(metric_id);
  key.set_epoch_index(epoch_index);
  return key;
}

PartialIngestionReport CreatePartialIngestionReport(absl::string_view run_id,
                                                    absl::Span<const PartitionSpec> partitions) {
  PartialIngestionReport report;
  RunFingerprint* const fp = report.mutable_partial_fingerprint();
  fp->set_run_id(std::string(run_id));
  fp->set_stage_id(Stage::STAGE_INGESTION);
  fp->set_is_partial(true);

  for (const auto& partition : partitions) {
    FingerprintEntry* const entry = fp->add_entries();
    *entry->mutable_metric_epoch_key() =
        CreateMetricEpochKey(partition.project_id, partition.metric_id, partition.epoch_index);
    entry->set_message_count(partition.message_count);

    MetricEpochSketch* const sketch_entry = report.add_metric_epoch_sketches();
    *sketch_entry->mutable_metric_epoch_key() =
        CreateMetricEpochKey(partition.project_id, partition.metric_id, partition.epoch_index);

    auto hll_sketch = HllSketch::Create(partition.sketch_precision).value();
    for (const auto& device_id : partition.device_ids) {
      hll_sketch.Insert(device_id);
    }
    *sketch_entry->mutable_hyperloglog_sketch() = hll_sketch.ToProto();
  }
  return report;
}

std::vector<std::string> GenerateDeviceIds(absl::string_view prefix, int count) {
  std::vector<std::string> device_ids;
  device_ids.reserve(count);
  for (int i = 0; i < count; ++i) {
    device_ids.push_back(absl::StrCat(prefix, "_device_", i));
  }
  return device_ids;
}

TEST(IngestionVerificationFnTest, SuccessfulConfigUnpackFollowedByDataRecords) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 50},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  const SignedRunContext signed_context =
      CreateValidSignedRunContext(1001, 2, "valid_signature_bytes");

  // Record 0: Ingests configuration
  ASSERT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              IsOk());

  EXPECT_TRUE(fn.HasConfig());
  ASSERT_TRUE(fn.Config().has_value());
  EXPECT_THAT(fn.Config()->signature(), Eq("valid_signature_bytes"));

  // Records 1..N: Ingests client data records
  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV("client-record-1"), context), IsOk());
  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV("client-record-2"), context), IsOk());
}

TEST(IngestionVerificationFnTest, CorruptedOrInvalidSignedRunContextPayloadReturnsError) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 50},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  // Invalid proto wire data on record 0
  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV("corrupted_payload_string"), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());
}

TEST(IngestionVerificationFnTest, CorruptedInnerRunContextReturnsError) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 50},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  // Outer proto is SignedRunContext, but serialized_run_context is corrupt
  SignedRunContext signed_context;
  signed_context.set_serialized_run_context("\xff\xff\xff\xff");
  signed_context.set_signature("valid_signature");

  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());
}

TEST(IngestionVerificationFnTest, InvalidRunContextInSignedRunContextReturnsError) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 50},
  };
  auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  SignedRunContext signed_context = CreateValidSignedRunContext(1001, 2, "");

  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());
}

TEST(IngestionVerificationFnTest, ConfigConstraintsAccessor) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 100},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  EXPECT_THAT(fn.ConfigConstraints().metric_thresholds(),
              ElementsAre(EquivToProto(constraints.metric_thresholds(0))));
}

TEST(IngestionVerificationFnTest, FinalizeReplicaWithoutConfigEmitsIntegrityFault) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 50},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  ASSERT_THAT(fn.InitializeReplica(google::protobuf::Any(), context), IsOk());
  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_EQ(context.Encrypted().size(), 1);
  EXPECT_EQ(context.Encrypted()[0].first, IngestionVerificationFn::kReencryptionKeyIndex);

  IngestionVerificationHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_TRUE(header.has_integrity_fault());

  IngestionVerificationPayload payload;
  ASSERT_TRUE(payload.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_TRUE(payload.has_integrity_fault());
}

TEST(IngestionVerificationFnTest, FinalizeReplicaWithCorruptedRecordEmitsIntegrityFault) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 50},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  const SignedRunContext signed_context =
      CreateValidSignedRunContext(1001, 2, "valid_signature_bytes");
  ASSERT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              IsOk());

  // Record 1 is corrupted bytes
  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV("corrupted_record_payload"), context), IsOk());
  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_EQ(context.Encrypted().size(), 1);
  IngestionVerificationHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_TRUE(header.has_integrity_fault());

  IngestionVerificationPayload payload;
  ASSERT_TRUE(payload.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_TRUE(payload.has_integrity_fault());
}

TEST(IngestionVerificationFnTest, FinalizeReplicaWithRunIdMismatchEmitsIntegrityFault) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 50},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  const SignedRunContext signed_context =
      CreateValidSignedRunContext(1001, 2, "valid_signature_bytes");
  ASSERT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              IsOk());

  const PartitionSpec partition_specs[] = {
      {.project_id = 1001,
       .metric_id = 2,
       .epoch_index = 100,
       .message_count = 10,
       .device_ids = GenerateDeviceIds("dev", 10),
       .sketch_precision = 14},
  };
  // Partial report specifies a mismatched run_id
  const PartialIngestionReport report =
      CreatePartialIngestionReport("different_run_id_9999", partition_specs);

  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(report.SerializeAsString()), context), IsOk());
  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_EQ(context.Encrypted().size(), 1);
  IngestionVerificationHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_TRUE(header.has_integrity_fault());

  IngestionVerificationPayload payload;
  ASSERT_TRUE(payload.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_TRUE(payload.has_integrity_fault());
}

TEST(IngestionVerificationFnTest, FinalizeReplicaWithInvalidPartialFingerprintEmitsIntegrityFault) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 50},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  const SignedRunContext signed_context =
      CreateValidSignedRunContext(1001, 2, "valid_signature_bytes");
  ASSERT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              IsOk());

  // Report with invalid stage_id in partial_fingerprint
  PartialIngestionReport report;
  report.mutable_partial_fingerprint()->set_run_id(std::string(kTestRunId));
  report.mutable_partial_fingerprint()->set_stage_id(Stage::STAGE_UNSPECIFIED);
  report.mutable_partial_fingerprint()->set_is_partial(true);

  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(report.SerializeAsString()), context), IsOk());
  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_EQ(context.Encrypted().size(), 1);
  IngestionVerificationHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_TRUE(header.has_integrity_fault());

  IngestionVerificationPayload payload;
  ASSERT_TRUE(payload.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_TRUE(payload.has_integrity_fault());
}

TEST(IngestionVerificationFnTest, FinalizeReplicaWithInvalidHllSketchEmitsIntegrityFault) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 50},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  const SignedRunContext signed_context =
      CreateValidSignedRunContext(1001, 2, "valid_signature_bytes");
  ASSERT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              IsOk());

  PartialIngestionReport report;
  report.mutable_partial_fingerprint()->set_run_id(std::string(kTestRunId));
  report.mutable_partial_fingerprint()->set_stage_id(Stage::STAGE_INGESTION);
  report.mutable_partial_fingerprint()->set_is_partial(true);

  MetricEpochSketch* const sketch = report.add_metric_epoch_sketches();
  *sketch->mutable_metric_epoch_key() = CreateMetricEpochKey(1001, 2, 100);
  sketch->mutable_hyperloglog_sketch()->set_precision(99);  // Invalid precision

  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(report.SerializeAsString()), context), IsOk());
  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_EQ(context.Encrypted().size(), 1);
  IngestionVerificationHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_TRUE(header.has_integrity_fault());

  IngestionVerificationPayload payload;
  ASSERT_TRUE(payload.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_TRUE(payload.has_integrity_fault());
}

TEST(IngestionVerificationFnTest, SuccessfulIngestionVerificationSinglePartitionMeetsThreshold) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 10},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  const SignedRunContext signed_context =
      CreateValidSignedRunContext(1001, 2, "valid_signature_bytes");
  ASSERT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              IsOk());

  const PartitionSpec partition_specs[] = {
      {.project_id = 1001,
       .metric_id = 2,
       .epoch_index = 50,
       .message_count = 15,
       .device_ids = GenerateDeviceIds("worker1", 15),
       .sketch_precision = 14},
  };
  const PartialIngestionReport report =
      CreatePartialIngestionReport(std::string(kTestRunId), partition_specs);

  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(report.SerializeAsString()), context), IsOk());
  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_EQ(context.Encrypted().size(), 1);
  EXPECT_EQ(context.Encrypted()[0].first, IngestionVerificationFn::kReencryptionKeyIndex);

  // Verify unencrypted header
  IngestionVerificationHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_FALSE(header.has_integrity_fault());
  ASSERT_TRUE(header.has_summary_report());
  ASSERT_EQ(header.summary_report().partition_counts_size(), 1);
  EXPECT_THAT(header.summary_report().partition_counts(0).metric_epoch_key(),
              EquivToProto(CreateMetricEpochKey(1001, 2, 50)));
  EXPECT_GE(header.summary_report().partition_counts(0).message_count(), 10);

  // Verify encrypted payload
  IngestionVerificationPayload payload;
  ASSERT_TRUE(payload.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_FALSE(payload.has_integrity_fault());
  ASSERT_TRUE(payload.has_success());

  const auto& success = payload.success();
  EXPECT_EQ(success.threshold_report().run_id(), std::string(kTestRunId));
  ASSERT_EQ(success.threshold_report().approved_partitions_size(), 1);
  EXPECT_THAT(success.threshold_report().approved_partitions(0),
              EquivToProto(CreateMetricEpochKey(1001, 2, 50)));

  EXPECT_EQ(success.canonical_fingerprint().run_id(), std::string(kTestRunId));
  EXPECT_EQ(success.canonical_fingerprint().stage_id(), Stage::STAGE_INGESTION);
  EXPECT_FALSE(success.canonical_fingerprint().is_partial());
  ASSERT_EQ(success.canonical_fingerprint().entries_size(), 1);
  EXPECT_THAT(success.canonical_fingerprint().entries(0).metric_epoch_key(),
              EquivToProto(CreateMetricEpochKey(1001, 2, 50)));
  EXPECT_EQ(success.canonical_fingerprint().entries(0).message_count(), 15);
  EXPECT_THAT(success.signed_run_context(), EquivToProto(signed_context));
}

TEST(IngestionVerificationFnTest, MultipleWorkersAndPartitionsWithThresholdFiltering) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 20},
      {.project_id = 1001, .metric_id = 3, .threshold = 50},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  const SignedRunContext signed_context =
      CreateValidSignedRunContext(1001, 2, "valid_signature_bytes");
  ASSERT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              IsOk());

  // Worker 1: 12 devices for metric 2 (partition A), 10 devices for metric 3 (partition B)
  const PartitionSpec worker1_specs[] = {
      {.project_id = 1001,
       .metric_id = 2,
       .epoch_index = 100,
       .message_count = 12,
       .device_ids = GenerateDeviceIds("w1_m2", 12),
       .sketch_precision = 14},
      {.project_id = 1001,
       .metric_id = 3,
       .epoch_index = 100,
       .message_count = 10,
       .device_ids = GenerateDeviceIds("w1_m3", 10),
       .sketch_precision = 14},
  };
  const PartialIngestionReport report1 =
      CreatePartialIngestionReport(std::string(kTestRunId), worker1_specs);
  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(report1.SerializeAsString()), context), IsOk());

  // Worker 2: 12 distinct devices for metric 2 (total ~24 >= 20 threshold -> APPROVED)
  //           10 distinct devices for metric 3 (total ~20 < 50 threshold -> DROPPED)
  const PartitionSpec worker2_specs[] = {
      {.project_id = 1001,
       .metric_id = 2,
       .epoch_index = 100,
       .message_count = 12,
       .device_ids = GenerateDeviceIds("w2_m2", 12),
       .sketch_precision = 14},
      {.project_id = 1001,
       .metric_id = 3,
       .epoch_index = 100,
       .message_count = 10,
       .device_ids = GenerateDeviceIds("w2_m3", 10),
       .sketch_precision = 14},
  };
  const PartialIngestionReport report2 =
      CreatePartialIngestionReport(std::string(kTestRunId), worker2_specs);
  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(report2.SerializeAsString()), context), IsOk());

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_EQ(context.Encrypted().size(), 1);

  // Header contains only the approved partition
  IngestionVerificationHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_FALSE(header.has_integrity_fault());
  ASSERT_TRUE(header.has_summary_report());
  ASSERT_EQ(header.summary_report().partition_counts_size(), 1);
  EXPECT_THAT(header.summary_report().partition_counts(0).metric_epoch_key(),
              EquivToProto(CreateMetricEpochKey(1001, 2, 100)));

  // Payload contains only the approved partition
  IngestionVerificationPayload payload;
  ASSERT_TRUE(payload.ParseFromString(context.Encrypted()[0].second.data));
  ASSERT_TRUE(payload.has_success());
  const auto& success = payload.success();

  ASSERT_EQ(success.threshold_report().approved_partitions_size(), 1);
  EXPECT_THAT(success.threshold_report().approved_partitions(0),
              EquivToProto(CreateMetricEpochKey(1001, 2, 100)));

  ASSERT_EQ(success.canonical_fingerprint().entries_size(), 1);
  EXPECT_THAT(success.canonical_fingerprint().entries(0).metric_epoch_key(),
              EquivToProto(CreateMetricEpochKey(1001, 2, 100)));
  EXPECT_EQ(success.canonical_fingerprint().entries(0).message_count(), 24);
  EXPECT_THAT(success.signed_run_context(), EquivToProto(signed_context));
}

TEST(IngestionVerificationFnTest, NoPartitionsMeetThresholdEmitsEmptySuccess) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 100},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  const SignedRunContext signed_context =
      CreateValidSignedRunContext(1001, 2, "valid_signature_bytes");
  ASSERT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              IsOk());

  const PartitionSpec partition_specs[] = {
      {.project_id = 1001,
       .metric_id = 2,
       .epoch_index = 50,
       .message_count = 5,
       .device_ids = GenerateDeviceIds("dev", 5),
       .sketch_precision = 14},
  };
  const PartialIngestionReport report =
      CreatePartialIngestionReport(std::string(kTestRunId), partition_specs);

  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(report.SerializeAsString()), context), IsOk());
  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_EQ(context.Encrypted().size(), 1);

  IngestionVerificationHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_FALSE(header.has_integrity_fault());
  ASSERT_TRUE(header.has_summary_report());
  EXPECT_EQ(header.summary_report().partition_counts_size(), 0);

  IngestionVerificationPayload payload;
  ASSERT_TRUE(payload.ParseFromString(context.Encrypted()[0].second.data));
  ASSERT_TRUE(payload.has_success());
  EXPECT_EQ(payload.success().threshold_report().approved_partitions_size(), 0);
  EXPECT_EQ(payload.success().canonical_fingerprint().entries_size(), 0);
  EXPECT_THAT(payload.success().signed_run_context(), EquivToProto(signed_context));
}

TEST(IngestionVerificationFnTest, ThresholdBoundaryConditionsExactMatchAndOneBelow) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 10},
      {.project_id = 1001, .metric_id = 3, .threshold = 10},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  const SignedRunContext signed_context =
      CreateValidSignedRunContext(1001, 2, "valid_signature_bytes");
  ASSERT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              IsOk());

  // Metric 2 has exactly 10 distinct devices (== threshold 10 -> APPROVED).
  // Metric 3 has exactly 9 distinct devices (== threshold - 1 -> DROPPED).
  const PartitionSpec partition_specs[] = {
      {.project_id = 1001,
       .metric_id = 2,
       .epoch_index = 100,
       .message_count = 10,
       .device_ids = GenerateDeviceIds("exact_m2", 10),
       .sketch_precision = 14},
      {.project_id = 1001,
       .metric_id = 3,
       .epoch_index = 100,
       .message_count = 9,
       .device_ids = GenerateDeviceIds("below_m3", 9),
       .sketch_precision = 14},
  };
  const PartialIngestionReport report =
      CreatePartialIngestionReport(std::string(kTestRunId), partition_specs);

  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(report.SerializeAsString()), context), IsOk());
  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_EQ(context.Encrypted().size(), 1);
  EXPECT_EQ(context.Encrypted()[0].first, IngestionVerificationFn::kReencryptionKeyIndex);

  // Unencrypted header summary report
  IngestionVerificationHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_FALSE(header.has_integrity_fault());
  ASSERT_TRUE(header.has_summary_report());
  ASSERT_EQ(header.summary_report().partition_counts_size(), 1);
  EXPECT_THAT(header.summary_report().partition_counts(0).metric_epoch_key(),
              EquivToProto(CreateMetricEpochKey(1001, 2, 100)));
  EXPECT_EQ(header.summary_report().partition_counts(0).message_count(), 10);

  // Encrypted payload
  IngestionVerificationPayload payload;
  ASSERT_TRUE(payload.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_FALSE(payload.has_integrity_fault());
  ASSERT_TRUE(payload.has_success());

  const auto& success = payload.success();
  ASSERT_EQ(success.threshold_report().approved_partitions_size(), 1);
  EXPECT_THAT(success.threshold_report().approved_partitions(0),
              EquivToProto(CreateMetricEpochKey(1001, 2, 100)));

  ASSERT_EQ(success.canonical_fingerprint().entries_size(), 1);
  EXPECT_THAT(success.canonical_fingerprint().entries(0).metric_epoch_key(),
              EquivToProto(CreateMetricEpochKey(1001, 2, 100)));
  EXPECT_EQ(success.canonical_fingerprint().entries(0).message_count(), 10);
  EXPECT_THAT(success.signed_run_context(), EquivToProto(signed_context));
}

TEST(IngestionVerificationFnTest, OverlappingDeviceIdsAcrossWorkersDeduplicatedBelowThreshold) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 20},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  const SignedRunContext signed_context =
      CreateValidSignedRunContext(1001, 2, "valid_signature_bytes");
  ASSERT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              IsOk());

  const std::vector<std::string> shared_device_ids = GenerateDeviceIds("overlap_dev", 10);

  // Worker 1: 10 distinct devices, message count 10
  const PartitionSpec worker1_specs[] = {
      {.project_id = 1001,
       .metric_id = 2,
       .epoch_index = 100,
       .message_count = 10,
       .device_ids = shared_device_ids,
       .sketch_precision = 14},
  };
  const PartialIngestionReport report1 =
      CreatePartialIngestionReport(std::string(kTestRunId), worker1_specs);
  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(report1.SerializeAsString()), context), IsOk());

  // Worker 2: Same 10 distinct devices, message count 10
  // Total message count across workers is 20 (>= threshold 20), but unique devices is 10 (< 20).
  const PartitionSpec worker2_specs[] = {
      {.project_id = 1001,
       .metric_id = 2,
       .epoch_index = 100,
       .message_count = 10,
       .device_ids = shared_device_ids,
       .sketch_precision = 14},
  };
  const PartialIngestionReport report2 =
      CreatePartialIngestionReport(std::string(kTestRunId), worker2_specs);
  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(report2.SerializeAsString()), context), IsOk());

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_EQ(context.Encrypted().size(), 1);

  IngestionVerificationHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_FALSE(header.has_integrity_fault());
  ASSERT_TRUE(header.has_summary_report());
  EXPECT_EQ(header.summary_report().partition_counts_size(), 0);

  IngestionVerificationPayload payload;
  ASSERT_TRUE(payload.ParseFromString(context.Encrypted()[0].second.data));
  ASSERT_TRUE(payload.has_success());
  EXPECT_EQ(payload.success().threshold_report().approved_partitions_size(), 0);
  EXPECT_EQ(payload.success().canonical_fingerprint().entries_size(), 0);
  EXPECT_THAT(payload.success().signed_run_context(), EquivToProto(signed_context));
}

TEST(IngestionVerificationFnTest, UnregisteredPartitionsNotInConfigConstraintsAreDropped) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 10},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  const SignedRunContext signed_context =
      CreateValidSignedRunContext(1001, 2, "valid_signature_bytes");
  ASSERT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              IsOk());

  const PartitionSpec partition_specs[] = {
      // Configured partition meeting threshold
      {.project_id = 1001,
       .metric_id = 2,
       .epoch_index = 100,
       .message_count = 15,
       .device_ids = GenerateDeviceIds("registered", 15),
       .sketch_precision = 14},
      // Unregistered project_id
      {.project_id = 9999,
       .metric_id = 2,
       .epoch_index = 100,
       .message_count = 50,
       .device_ids = GenerateDeviceIds("unknown_project", 50),
       .sketch_precision = 14},
      // Unregistered metric_id
      {.project_id = 1001,
       .metric_id = 999,
       .epoch_index = 100,
       .message_count = 50,
       .device_ids = GenerateDeviceIds("unknown_metric", 50),
       .sketch_precision = 14},
  };
  const PartialIngestionReport report =
      CreatePartialIngestionReport(std::string(kTestRunId), partition_specs);

  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(report.SerializeAsString()), context), IsOk());
  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_EQ(context.Encrypted().size(), 1);

  IngestionVerificationHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_FALSE(header.has_integrity_fault());
  ASSERT_TRUE(header.has_summary_report());
  ASSERT_EQ(header.summary_report().partition_counts_size(), 1);
  EXPECT_THAT(header.summary_report().partition_counts(0).metric_epoch_key(),
              EquivToProto(CreateMetricEpochKey(1001, 2, 100)));

  IngestionVerificationPayload payload;
  ASSERT_TRUE(payload.ParseFromString(context.Encrypted()[0].second.data));
  ASSERT_TRUE(payload.has_success());
  const auto& success = payload.success();

  ASSERT_EQ(success.threshold_report().approved_partitions_size(), 1);
  EXPECT_THAT(success.threshold_report().approved_partitions(0),
              EquivToProto(CreateMetricEpochKey(1001, 2, 100)));

  ASSERT_EQ(success.canonical_fingerprint().entries_size(), 1);
  EXPECT_THAT(success.canonical_fingerprint().entries(0).metric_epoch_key(),
              EquivToProto(CreateMetricEpochKey(1001, 2, 100)));
  EXPECT_EQ(success.canonical_fingerprint().entries(0).message_count(), 15);
  EXPECT_THAT(success.signed_run_context(), EquivToProto(signed_context));
}

TEST(IngestionVerificationFnTest, ZeroDataRecordsAfterConfigurationEmitsEmptySuccess) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 10},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  const SignedRunContext signed_context =
      CreateValidSignedRunContext(1001, 2, "valid_signature_bytes");
  ASSERT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              IsOk());

  // FinalizeReplica called with zero data records processed
  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_EQ(context.Encrypted().size(), 1);
  EXPECT_EQ(context.Encrypted()[0].first, IngestionVerificationFn::kReencryptionKeyIndex);

  IngestionVerificationHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_FALSE(header.has_integrity_fault());
  ASSERT_TRUE(header.has_summary_report());
  EXPECT_EQ(header.summary_report().partition_counts_size(), 0);

  IngestionVerificationPayload payload;
  ASSERT_TRUE(payload.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_FALSE(payload.has_integrity_fault());
  ASSERT_TRUE(payload.has_success());

  const auto& success = payload.success();
  EXPECT_EQ(success.threshold_report().run_id(), std::string(kTestRunId));
  EXPECT_EQ(success.threshold_report().approved_partitions_size(), 0);

  EXPECT_EQ(success.canonical_fingerprint().run_id(), std::string(kTestRunId));
  EXPECT_EQ(success.canonical_fingerprint().stage_id(), Stage::STAGE_INGESTION);
  EXPECT_FALSE(success.canonical_fingerprint().is_partial());
  EXPECT_EQ(success.canonical_fingerprint().entries_size(), 0);
  EXPECT_THAT(success.signed_run_context(), EquivToProto(signed_context));
}

TEST(IngestionVerificationFnTest, IncompatibleHllSketchPrecisionAcrossWorkersEmitsIntegrityFault) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 10},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  const SignedRunContext signed_context =
      CreateValidSignedRunContext(1001, 2, "valid_signature_bytes");
  ASSERT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              IsOk());

  // Worker 1 sends HllSketch with precision 14
  const PartitionSpec worker1_specs[] = {
      {.project_id = 1001,
       .metric_id = 2,
       .epoch_index = 100,
       .message_count = 10,
       .device_ids = GenerateDeviceIds("w1", 10),
       .sketch_precision = 14},
  };
  const PartialIngestionReport report1 =
      CreatePartialIngestionReport(std::string(kTestRunId), worker1_specs);
  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(report1.SerializeAsString()), context), IsOk());

  // Worker 2 sends HllSketch for same partition with precision 12 (incompatible with precision 14)
  const PartitionSpec worker2_specs[] = {
      {.project_id = 1001,
       .metric_id = 2,
       .epoch_index = 100,
       .message_count = 10,
       .device_ids = GenerateDeviceIds("w2", 10),
       .sketch_precision = 12},
  };
  const PartialIngestionReport report2 =
      CreatePartialIngestionReport(std::string(kTestRunId), worker2_specs);
  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(report2.SerializeAsString()), context), IsOk());

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_EQ(context.Encrypted().size(), 1);

  IngestionVerificationHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_TRUE(header.has_integrity_fault());

  IngestionVerificationPayload payload;
  ASSERT_TRUE(payload.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_TRUE(payload.has_integrity_fault());
  EXPECT_FALSE(payload.has_success());
}

TEST(IngestionVerificationFnTest, SubsequentRecordProcessingAfterFaultPreservesIntegrityFault) {
  const MetricThresholdSpec specs[] = {
      {.project_id = 1001, .metric_id = 2, .threshold = 10},
  };
  const auto constraints = CreateConfigConstraints(specs);
  IngestionVerificationFn fn(constraints);
  FakeContext context;

  const SignedRunContext signed_context =
      CreateValidSignedRunContext(1001, 2, "valid_signature_bytes");
  ASSERT_THAT(fn.Do(IngestionVerificationFn::KV(signed_context.SerializeAsString()), context),
              IsOk());

  // Record 1: Corrupted payload triggers integrity fault
  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV("corrupted_record_payload"), context), IsOk());

  // Record 2: Valid PartialIngestionReport exceeding threshold
  const PartitionSpec partition_specs[] = {
      {.project_id = 1001,
       .metric_id = 2,
       .epoch_index = 100,
       .message_count = 15,
       .device_ids = GenerateDeviceIds("valid_dev", 15),
       .sketch_precision = 14},
  };
  const PartialIngestionReport valid_report =
      CreatePartialIngestionReport(std::string(kTestRunId), partition_specs);
  EXPECT_THAT(fn.Do(IngestionVerificationFn::KV(valid_report.SerializeAsString()), context),
              IsOk());

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), context), IsOk());

  ASSERT_EQ(context.Encrypted().size(), 1);

  IngestionVerificationHeader header;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&header));
  EXPECT_TRUE(header.has_integrity_fault());

  IngestionVerificationPayload payload;
  ASSERT_TRUE(payload.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_TRUE(payload.has_integrity_fault());
  EXPECT_FALSE(payload.has_success());
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
