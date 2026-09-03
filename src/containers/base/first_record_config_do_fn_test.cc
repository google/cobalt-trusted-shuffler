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

#include "src/containers/base/first_record_config_do_fn.h"

#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/fake_context.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cobalt::trusted_shuffler::testing::FakeContext;
using ::testing::Eq;
using ::testing::SizeIs;

// Test implementation of FirstRecordConfigDoFn using RunConfig as config proto.
class TestFirstRecordDoFn : public FirstRecordConfigDoFn<RunConfig> {
 public:
  TestFirstRecordDoFn() = default;

  const std::vector<std::string>& ReceivedRecords() const { return received_records_; }

 protected:
  absl::Status OnConfig(const RunConfig& config, Context& context) override {
    if (config.target_batch_size() < 0) {
      return absl::InvalidArgumentError("Invalid target_batch_size");
    }
    return absl::OkStatus();
  }

  absl::Status OnRecord(KV record, Context& context) override {
    received_records_.push_back(std::move(record.data));
    return absl::OkStatus();
  }

 private:
  std::vector<std::string> received_records_;
};

RunConfig CreateValidConfig(int32_t project_id, int32_t metric_id, int32_t batch_size) {
  RunConfig config;
  config.set_target_batch_size(batch_size);
  auto* metric = config.add_metric_identifiers();
  metric->set_project_id(project_id);
  metric->set_metric_id(metric_id);
  return config;
}

TEST(FirstRecordConfigDoFnTest, IngestsConfigOnRecordZeroAndRoutesSubsequentRecords) {
  TestFirstRecordDoFn fn;
  FakeContext context;

  EXPECT_FALSE(fn.HasConfig());

  RunConfig config = CreateValidConfig(101, 202, 50);
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV(config.SerializeAsString()), context), IsOk());

  EXPECT_TRUE(fn.HasConfig());
  ASSERT_TRUE(fn.Config().has_value());
  EXPECT_THAT(fn.Config()->target_batch_size(), Eq(50));

  // Records 1..N routed to OnRecord
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV("data-record-1"), context), IsOk());
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV("data-record-2"), context), IsOk());
  EXPECT_THAT(fn.ReceivedRecords(), SizeIs(2));
}

TEST(FirstRecordConfigDoFnTest, FailsOnEmptyFirstRecordAndRejectsSubsequentRecords) {
  TestFirstRecordDoFn fn;
  FakeContext context;

  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV(""), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());

  // One-shot semantics: subsequent records are rejected.
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV("data"), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(FirstRecordConfigDoFnTest, FailsOnCorruptedConfigProtoAndRejectsSubsequentRecords) {
  TestFirstRecordDoFn fn;
  FakeContext context;

  // Invalid proto wire data
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV("\xff\xff\xff\xff"), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());

  // One-shot semantics: subsequent records are rejected.
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV("data"), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(FirstRecordConfigDoFnTest, OnConfigFailureRejectsSubsequentRecords) {
  TestFirstRecordDoFn fn;
  FakeContext context;

  RunConfig config = CreateValidConfig(101, 202, -5);  // Causes OnConfig to fail
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV(config.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());

  // One-shot semantics: subsequent records (even if valid config) are rejected.
  RunConfig valid_config = CreateValidConfig(101, 202, 50);
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV(valid_config.SerializeAsString()), context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(fn.ReceivedRecords(), ::testing::IsEmpty());
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
