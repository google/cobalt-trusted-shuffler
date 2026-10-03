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

#include "src/testing/integration/do_fn_runner.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "containers/fns/do_fn.h"
#include "containers/session.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "gtest/gtest.h"
#include "src/containers/base/first_record_config_do_fn.h"
#include "src/containers/base/pipeline_nodes.h"
#include "src/containers/base/with_outputs.h"
#include "src/proto/common.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::IsOkAndHolds;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::SizeIs;

// Fake DoFn that records lifecycle calls and allows injecting non-OK statuses
// at InitializeReplica, Do (for a specific record index), or FinalizeReplica.
class FakeLifecycleDoFn : public TrustedDoFn::WithInputs<Node::kClientUploads>::WithOutputs<
                              EncryptsTo<Node::kCountedMessages>,
                              Unencrypted<confidential_federated_compute::Session::KV>> {
 public:
  struct Config {
    absl::Status init_status = absl::OkStatus();
    int fail_on_do_index = -1;
    absl::Status do_fail_status = absl::InternalError("Do failed");
    absl::Status finalize_status = absl::OkStatus();
  };

  explicit FakeLifecycleDoFn(Config config) : config_(config) {}

  absl::Status InitializeReplica(google::protobuf::Any /*config*/, Context& /*context*/) override {
    init_called_ = true;
    return config_.init_status;
  }

  absl::Status Do(confidential_federated_compute::Session::KV input, DoContext& context) override {
    const int current_index = do_call_count_++;
    if (current_index == config_.fail_on_do_index) {
      return config_.do_fail_status;
    }
    context.EmitEncrypted(/*reencryption_key_index=*/0, std::move(input));
    return absl::OkStatus();
  }

  absl::Status FinalizeReplica(google::protobuf::Any /*config*/, FnContext& context) override {
    finalize_called_ = true;
    if (!config_.finalize_status.ok()) {
      return config_.finalize_status;
    }
    context.EmitUnencrypted(confidential_federated_compute::Session::KV("finalize_summary"));
    return absl::OkStatus();
  }

  bool init_called() const { return init_called_; }
  int do_call_count() const { return do_call_count_; }
  bool finalize_called() const { return finalize_called_; }

 private:
  Config config_;
  bool init_called_ = false;
  int do_call_count_ = 0;
  bool finalize_called_ = false;
};

TEST(DoFnRunnerTest, InitializeReplicaFailsAbortsBeforeDoAndFinalize) {
  FakeLifecycleDoFn do_fn({
      .init_status = absl::InvalidArgumentError("Invalid init config"),
  });
  const KVCollection inputs = {
      EncryptedBlob(Node::kClientUploads, "record_0").ToKV(),
  };

  const absl::StatusOr<BlobStageOutputs> result =
      DoFnRunner::RunReplicaWithBlobOutput(do_fn, {.inputs = inputs});

  EXPECT_THAT(result.status(), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_TRUE(do_fn.init_called());
  EXPECT_EQ(do_fn.do_call_count(), 0);
  EXPECT_FALSE(do_fn.finalize_called());
}

TEST(DoFnRunnerTest, DoFailsOnMiddleRecordStopsExecutionSkipsFinalizeAndDiscardsOutputs) {
  FakeLifecycleDoFn do_fn({
      .fail_on_do_index = 1,
      .do_fail_status = absl::DataLossError("Corrupt record"),
  });
  const KVCollection inputs = {
      EncryptedBlob(Node::kClientUploads, "record_0").ToKV(),
      EncryptedBlob(Node::kClientUploads, "record_1").ToKV(),
      EncryptedBlob(Node::kClientUploads, "record_2").ToKV(),
  };

  const absl::StatusOr<KVBlobStageOutputs> result =
      DoFnRunner::RunReplicaWithKVBlobOutput(do_fn, {.inputs = inputs});

  // Matches host pipeline AbortReplica: stops immediately on record 1 without processing
  // record 2 or calling FinalizeReplica, and discards record 0's emitted output.
  EXPECT_THAT(result.status(), StatusIs(absl::StatusCode::kDataLoss));
  EXPECT_TRUE(do_fn.init_called());
  EXPECT_EQ(do_fn.do_call_count(), 2);
  EXPECT_FALSE(do_fn.finalize_called());
}

TEST(DoFnRunnerTest, FinalizeReplicaFailsDiscardsEmittedOutputs) {
  FakeLifecycleDoFn do_fn({
      .finalize_status = absl::FailedPreconditionError("Batch size mismatch"),
  });
  const KVCollection inputs = {
      EncryptedBlob(Node::kClientUploads, "record_0").ToKV(),
      EncryptedBlob(Node::kClientUploads, "record_1").ToKV(),
  };

  const absl::StatusOr<KVBlobStageOutputs> result =
      DoFnRunner::RunReplicaWithKVBlobOutput(do_fn, {.inputs = inputs});

  EXPECT_THAT(result.status(), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_TRUE(do_fn.init_called());
  EXPECT_EQ(do_fn.do_call_count(), 2);
  EXPECT_TRUE(do_fn.finalize_called());
}

TEST(DoFnRunnerTest, EmptyInputStreamRunsInitializeAndFinalizeWithoutCallingDo) {
  FakeLifecycleDoFn do_fn({});
  const absl::StatusOr<BlobStageOutputs> result =
      DoFnRunner::RunReplicaWithBlobOutput(do_fn, {.inputs = {}});

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(do_fn.init_called());
  EXPECT_EQ(do_fn.do_call_count(), 0);
  EXPECT_TRUE(do_fn.finalize_called());
  EXPECT_EQ(result->unencrypted.size(), 1);
  EXPECT_EQ(result->unencrypted[0].plaintext(), "finalize_summary");
}

TEST(DoFnRunnerTest, InputFromUndeclaredNodeReturnsPermissionDeniedAndSkipsDoAndFinalize) {
  FakeLifecycleDoFn do_fn({});
  const KVCollection inputs = {
      EncryptedBlob(Node::kClientUploads, "record_0").ToKV(),
      EncryptedBlob(Node::kRunContext, "misrouted_record").ToKV(),
      EncryptedBlob(Node::kClientUploads, "record_2").ToKV(),
  };

  const absl::StatusOr<KVBlobStageOutputs> result =
      DoFnRunner::RunReplicaWithKVBlobOutput(do_fn, {.inputs = inputs});

  EXPECT_THAT(result.status(),
              StatusIs(absl::StatusCode::kPermissionDenied,
                       HasSubstr("Session does not accept input from node kRunContext (1).")));
  EXPECT_TRUE(do_fn.init_called());
  EXPECT_EQ(do_fn.do_call_count(), 1);
  EXPECT_FALSE(do_fn.finalize_called());
}

TEST(DoFnRunnerTest, InputFromUnencryptedNodeReturnsPermissionDeniedWithSpecificMessage) {
  FakeLifecycleDoFn do_fn({});
  const KVCollection inputs = {
      EncryptedBlob(Node::kClientUploads, "record_0").ToKV(),
      RoutedKV{
          .node = Node::kNone,
          .key = {},
          .data = "unencrypted_record",
      },
  };

  const absl::StatusOr<KVBlobStageOutputs> result =
      DoFnRunner::RunReplicaWithKVBlobOutput(do_fn, {.inputs = inputs});

  EXPECT_THAT(result.status(),
              StatusIs(absl::StatusCode::kPermissionDenied,
                       HasSubstr("Session does not accept unencrypted input (Node::kNone).")));
}

TEST(DoFnRunnerTest, TagsEmittedEncryptedRecordsWithDeclaredDestinationNode) {
  FakeLifecycleDoFn do_fn({});
  const KVCollection inputs = {
      EncryptedBlob(Node::kClientUploads, "record_0").ToKV(),
  };

  const absl::StatusOr<BlobStageOutputs> result =
      DoFnRunner::RunReplicaWithBlobOutput(do_fn, {.inputs = inputs});

  ASSERT_THAT(result, IsOk());
  ASSERT_THAT(result->encrypted, SizeIs(1));
  EXPECT_THAT(result->encrypted[0].node(), Eq(Node::kCountedMessages));
  EXPECT_THAT(result->encrypted[0].ciphertext(), Eq("record_0"));
}

class FakeFirstRecordDoFn
    : public FirstRecordConfigDoFn<ConfigFrom<RunConfig, Node::kRunContext>,
                                   RecordsFrom<ShuffledMessage, Node::kClientUploads>>::
          WithOutputs<EncryptsTo<>, Unencrypted<IntegrityFault>> {
 public:
  FakeFirstRecordDoFn() = default;

  bool ConfigCalled() const { return config_called_; }
  int RecordCallCount() const { return record_call_count_; }

 protected:
  absl::Status OnConfig(const RunConfig& /*config*/, DoContext& /*context*/) override {
    config_called_ = true;
    return absl::OkStatus();
  }

  absl::Status OnRecord(const ShuffledMessage& /*record*/, DoContext& /*context*/) override {
    ++record_call_count_;
    return absl::OkStatus();
  }

 private:
  bool config_called_ = false;
  int record_call_count_ = 0;
};

class FakeConfigOnlyDoFn
    : public FirstRecordConfigDoFn<ConfigFrom<RunConfig, Node::kRunContext>, NoRecords>::
          WithOutputs<EncryptsTo<>, Unencrypted<IntegrityFault>> {
 public:
  FakeConfigOnlyDoFn() = default;

  bool ConfigCalled() const { return config_called_; }

 protected:
  absl::Status OnConfig(const RunConfig& /*config*/, DoContext& /*context*/) override {
    config_called_ = true;
    return absl::OkStatus();
  }

 private:
  bool config_called_ = false;
};

RunConfig ValidTestConfig() {
  RunConfig config;
  config.set_target_batch_size(10);
  return config;
}

ShuffledMessage ValidTestMessage() {
  ShuffledMessage msg;
  msg.set_customer_message("test_payload");
  return msg;
}

TEST(DoFnRunnerTest, FirstRecordConfigDoFnRejectsRecordZeroFromWrongNode) {
  FakeFirstRecordDoFn do_fn;
  const KVCollection inputs = {
      EncryptedBlob(Node::kClientUploads, ValidTestConfig().SerializeAsString()).ToKV(),
  };

  const absl::StatusOr<BlobStageOutputs> result =
      DoFnRunner::RunReplicaWithBlobOutput(do_fn, {.inputs = inputs});

  EXPECT_THAT(result.status(),
              StatusIs(absl::StatusCode::kPermissionDenied,
                       HasSubstr("Session does not accept input from node kClientUploads (0).")));
  EXPECT_FALSE(do_fn.ConfigCalled());
}

TEST(DoFnRunnerTest, FirstRecordConfigDoFnRejectsUnencryptedRecordZero) {
  FakeFirstRecordDoFn do_fn;
  const KVCollection inputs = {
      RoutedKV{
          .node = Node::kNone,
          .key = {},
          .data = ValidTestConfig().SerializeAsString(),
      },
  };

  const absl::StatusOr<BlobStageOutputs> result =
      DoFnRunner::RunReplicaWithBlobOutput(do_fn, {.inputs = inputs});

  EXPECT_THAT(result.status(),
              StatusIs(absl::StatusCode::kPermissionDenied,
                       HasSubstr("Session does not accept unencrypted input (Node::kNone).")));
  EXPECT_FALSE(do_fn.ConfigCalled());
}

TEST(DoFnRunnerTest, FirstRecordConfigDoFnRejectsSubsequentRecordFromConfigSource) {
  FakeFirstRecordDoFn do_fn;
  const KVCollection inputs = {
      EncryptedBlob(Node::kRunContext, ValidTestConfig().SerializeAsString()).ToKV(),
      EncryptedBlob(Node::kRunContext, ValidTestMessage().SerializeAsString()).ToKV(),
  };

  const absl::StatusOr<BlobStageOutputs> result =
      DoFnRunner::RunReplicaWithBlobOutput(do_fn, {.inputs = inputs});

  EXPECT_THAT(result.status(),
              StatusIs(absl::StatusCode::kPermissionDenied,
                       HasSubstr("Session does not accept input from node kRunContext (1).")));
  EXPECT_TRUE(do_fn.ConfigCalled());
  EXPECT_EQ(do_fn.RecordCallCount(), 0);
}

TEST(DoFnRunnerTest, FirstRecordConfigDoFnRejectsUnencryptedSubsequentRecord) {
  FakeFirstRecordDoFn do_fn;
  const KVCollection inputs = {
      EncryptedBlob(Node::kRunContext, ValidTestConfig().SerializeAsString()).ToKV(),
      RoutedKV{
          .node = Node::kNone,
          .key = {},
          .data = ValidTestMessage().SerializeAsString(),
      },
  };

  const absl::StatusOr<BlobStageOutputs> result =
      DoFnRunner::RunReplicaWithBlobOutput(do_fn, {.inputs = inputs});

  EXPECT_THAT(result.status(),
              StatusIs(absl::StatusCode::kPermissionDenied,
                       HasSubstr("Session does not accept unencrypted input (Node::kNone).")));
  EXPECT_TRUE(do_fn.ConfigCalled());
  EXPECT_EQ(do_fn.RecordCallCount(), 0);
}

TEST(DoFnRunnerTest, FirstRecordConfigDoFnAcceptsValidPhasedInputs) {
  FakeFirstRecordDoFn do_fn;
  const KVCollection inputs = {
      EncryptedBlob(Node::kRunContext, ValidTestConfig().SerializeAsString()).ToKV(),
      EncryptedBlob(Node::kClientUploads, ValidTestMessage().SerializeAsString()).ToKV(),
  };

  const absl::StatusOr<BlobStageOutputs> result =
      DoFnRunner::RunReplicaWithBlobOutput(do_fn, {.inputs = inputs});

  ASSERT_THAT(result, IsOk());
  EXPECT_TRUE(do_fn.ConfigCalled());
  EXPECT_EQ(do_fn.RecordCallCount(), 1);
}

TEST(DoFnRunnerTest, FirstRecordConfigDoFnWorksWithKVBlobOutput) {
  FakeFirstRecordDoFn do_fn;
  const KVCollection inputs = {
      EncryptedBlob(Node::kRunContext, ValidTestConfig().SerializeAsString()).ToKV(),
      EncryptedBlob(Node::kClientUploads, ValidTestMessage().SerializeAsString()).ToKV(),
  };

  const absl::StatusOr<KVBlobStageOutputs> result =
      DoFnRunner::RunReplicaWithKVBlobOutput(do_fn, {.inputs = inputs});

  ASSERT_THAT(result, IsOk());
  EXPECT_TRUE(do_fn.ConfigCalled());
  EXPECT_EQ(do_fn.RecordCallCount(), 1);
}

TEST(DoFnRunnerTest, ConfigOnlyFirstRecordConfigDoFnAcceptsSingleConfigRecord) {
  FakeConfigOnlyDoFn do_fn;
  const KVCollection inputs = {
      EncryptedBlob(Node::kRunContext, ValidTestConfig().SerializeAsString()).ToKV(),
  };

  const absl::StatusOr<BlobStageOutputs> result =
      DoFnRunner::RunReplicaWithBlobOutput(do_fn, {.inputs = inputs});

  ASSERT_THAT(result, IsOk());
  EXPECT_TRUE(do_fn.ConfigCalled());
}

TEST(DoFnRunnerTest, ConfigOnlyFirstRecordConfigDoFnRejectsSubsequentRecords) {
  FakeConfigOnlyDoFn do_fn;
  const KVCollection inputs = {
      EncryptedBlob(Node::kRunContext, ValidTestConfig().SerializeAsString()).ToKV(),
      EncryptedBlob(Node::kClientUploads, "unexpected_data").ToKV(),
  };

  const absl::StatusOr<BlobStageOutputs> result =
      DoFnRunner::RunReplicaWithBlobOutput(do_fn, {.inputs = inputs});

  EXPECT_THAT(result.status(),
              StatusIs(absl::StatusCode::kPermissionDenied,
                       HasSubstr("Session does not accept input from node kClientUploads (0).")));
  EXPECT_TRUE(do_fn.ConfigCalled());
}

TEST(DoFnRunnerTest, ConfigOnlyFirstRecordConfigDoFnRejectsSubsequentRecordFromConfigSource) {
  FakeConfigOnlyDoFn do_fn;
  const KVCollection inputs = {
      EncryptedBlob(Node::kRunContext, ValidTestConfig().SerializeAsString()).ToKV(),
      EncryptedBlob(Node::kRunContext, ValidTestConfig().SerializeAsString()).ToKV(),
  };

  const absl::StatusOr<BlobStageOutputs> result =
      DoFnRunner::RunReplicaWithBlobOutput(do_fn, {.inputs = inputs});

  EXPECT_THAT(result.status(),
              StatusIs(absl::StatusCode::kPermissionDenied,
                       HasSubstr("Session does not accept input from node kRunContext (1).")));
  EXPECT_TRUE(do_fn.ConfigCalled());
}

}  // namespace
}  // namespace cobalt::trusted_shuffler::testing::integration
