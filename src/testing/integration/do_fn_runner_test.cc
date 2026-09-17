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
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {
namespace {

using ::absl_testing::StatusIs;

// Fake DoFn that records lifecycle calls and allows injecting non-OK statuses
// at InitializeReplica, Do (for a specific record index), or FinalizeReplica.
class FakeLifecycleDoFn : public confidential_federated_compute::fns::DoFn {
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

  absl::Status Do(confidential_federated_compute::Session::KV input, Context& context) override {
    const int current_index = do_call_count_++;
    if (current_index == config_.fail_on_do_index) {
      return config_.do_fail_status;
    }
    context.EmitEncrypted(/*reencryption_key_index=*/0, std::move(input));
    return absl::OkStatus();
  }

  absl::Status FinalizeReplica(google::protobuf::Any /*config*/, Context& context) override {
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
  const std::vector<confidential_federated_compute::Session::KV> inputs = {
      confidential_federated_compute::Session::KV("record_0"),
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
  const std::vector<confidential_federated_compute::Session::KV> inputs = {
      confidential_federated_compute::Session::KV("record_0"),
      confidential_federated_compute::Session::KV("record_1"),
      confidential_federated_compute::Session::KV("record_2"),
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
  const std::vector<confidential_federated_compute::Session::KV> inputs = {
      confidential_federated_compute::Session::KV("record_0"),
      confidential_federated_compute::Session::KV("record_1"),
  };

  const absl::StatusOr<KVBlobStageOutputs> result =
      DoFnRunner::RunReplicaWithKVBlobOutput(do_fn, {.inputs = inputs});

  EXPECT_THAT(result.status(), StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_TRUE(do_fn.init_called());
  EXPECT_EQ(do_fn.do_call_count(), 2);
  EXPECT_TRUE(do_fn.finalize_called());
}

}  // namespace
}  // namespace cobalt::trusted_shuffler::testing::integration
