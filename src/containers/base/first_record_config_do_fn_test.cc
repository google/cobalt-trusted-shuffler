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

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/containers/base/pipeline_nodes.h"
#include "src/containers/base/with_outputs.h"
#include "src/proto/common.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/fake_context.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cobalt::trusted_shuffler::testing::FakeContext;
using ::cobalt::trusted_shuffler::testing::FakeDoContext;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;

// A misdeclared container is rejected when it is compiled, which no runtime
// test can reach, so the predicates behind each rejection are pinned here in
// both polarities.
namespace compile_time_contract {

// The first template argument is the configuration proto and the data node it
// is read from.
static_assert(internal::kIsConfigFrom<ConfigFrom<RunConfig, Node::kRunContext>>);
static_assert(!internal::kIsConfigFrom<RunConfig>);
static_assert(!internal::kIsConfigFrom<RecordsFrom<RunConfig, Node::kRunContext>>);

// The second template argument is the record type and the data nodes it is read
// from, which a container that reads only its configuration leaves empty.
static_assert(internal::kIsRecordsFrom<RecordsFrom<ShuffledMessage, Node::kIngestedMessages>>);
static_assert(internal::kIsRecordsFrom<RecordsFrom<ShuffledMessage>>);
static_assert(!internal::kIsRecordsFrom<ShuffledMessage>);
static_assert(!internal::kIsRecordsFrom<ConfigFrom<ShuffledMessage, Node::kIngestedMessages>>);

// The configuration and the records are read from different data nodes.
static_assert(internal::kAreDisjointSources<Node::kVerifiedIngestion>);
static_assert(internal::kAreDisjointSources<Node::kVerifiedIngestion, Node::kFilteredMessages,
                                            Node::kPaddingMessages>);
static_assert(!internal::kAreDisjointSources<Node::kVerifiedIngestion, Node::kVerifiedIngestion>);
static_assert(!internal::kAreDisjointSources<Node::kVerifiedIngestion, Node::kIngestedMessages,
                                             Node::kVerifiedIngestion>);

// ConfigFrom exposes kSource, and RecordsFrom exposes kSources.
static_assert(ConfigFrom<RunConfig, Node::kRunContext>::kSource == Node::kRunContext);
static_assert(RecordsFrom<ShuffledMessage, Node::kIngestedMessages>::kSources ==
              std::array{Node::kIngestedMessages});
static_assert(internal::kIsRecordsFrom<NoRecords>);
static_assert(NoRecords::kSources.empty());

// A container reads from the data node of its configuration and the data nodes
// of its records, listed together in ascending order.
using InterleavedSourcesDoFn = FirstRecordConfigDoFn<
    ConfigFrom<RunConfig, Node::kVerifiedIngestion>,
    RecordsFrom<ShuffledMessage, Node::kIngestedMessages,
                Node::kFilteredMessages>>::WithOutputs<EncryptsTo<>, Unencrypted<IntegrityFault>>;
static_assert(InterleavedSourcesDoFn::kSources == std::array{Node::kIngestedMessages,
                                                             Node::kVerifiedIngestion,
                                                             Node::kFilteredMessages});
static_assert(InterleavedSourcesDoFn::kConfigSource == Node::kVerifiedIngestion);
static_assert(InterleavedSourcesDoFn::kRecordSources ==
              std::array{Node::kIngestedMessages, Node::kFilteredMessages});

// A container that reads only its configuration reads from that data node
// alone.
using ConfigOnlyDoFn =
    FirstRecordConfigDoFn<ConfigFrom<RunConfig, Node::kVerifiedIngestion>,
                          NoRecords>::WithOutputs<EncryptsTo<>, Unencrypted<IntegrityFault>>;
static_assert(ConfigOnlyDoFn::kSources == std::array{Node::kVerifiedIngestion});
static_assert(ConfigOnlyDoFn::kConfigSource == Node::kVerifiedIngestion);
static_assert(ConfigOnlyDoFn::kRecordSources.empty());

}  // namespace compile_time_contract

// Test implementation of FirstRecordConfigDoFn using RunConfig as config proto
// and ShuffledMessage as record proto.
class TestFirstRecordDoFn
    : public FirstRecordConfigDoFn<ConfigFrom<RunConfig, Node::kRunContext>,
                                   RecordsFrom<ShuffledMessage, Node::kIngestedMessages>> {
 public:
  TestFirstRecordDoFn() = default;

  const std::vector<ShuffledMessage>& ReceivedRecords() const { return received_records_; }

 protected:
  absl::Status OnConfig(const RunConfig& config, DoContext& context) override {
    if (config.target_batch_size() <= 0) {
      return absl::InvalidArgumentError("Invalid target_batch_size");
    }
    return absl::OkStatus();
  }

  absl::Status OnRecord(const ShuffledMessage& record, DoContext& context) override {
    if (record.shuffle_count() < 0) {
      return absl::InvalidArgumentError("Invalid shuffle_count");
    }
    received_records_.push_back(record);
    return absl::OkStatus();
  }

 private:
  std::vector<ShuffledMessage> received_records_;
};

RunConfig CreateValidConfig(int32_t project_id, int32_t metric_id, int32_t batch_size) {
  RunConfig config;
  config.set_target_batch_size(batch_size);
  auto* metric = config.add_metric_identifiers();
  metric->set_project_id(project_id);
  metric->set_metric_id(metric_id);
  return config;
}

ShuffledMessage CreateMessage(const std::string& run_id, int32_t group_id, int32_t shuffle_count,
                              const std::string& payload) {
  ShuffledMessage message;
  message.set_run_id(run_id);
  message.set_group_id(group_id);
  message.set_shuffle_count(shuffle_count);
  message.set_customer_message(payload);
  return message;
}

TEST(FirstRecordConfigDoFnTest, IngestsConfigOnRecordZeroAndRoutesSubsequentRecords) {
  TestFirstRecordDoFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  EXPECT_FALSE(fn.HasConfig());
  EXPECT_FALSE(fn.HasConfigFailure());

  RunConfig config = CreateValidConfig(101, 202, 50);
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV(config.SerializeAsString()), do_context), IsOk());

  EXPECT_TRUE(fn.HasConfig());
  EXPECT_FALSE(fn.HasConfigFailure());
  ASSERT_TRUE(fn.Config().has_value());
  EXPECT_THAT(fn.Config()->target_batch_size(), Eq(50));

  // Records 1..N parsed as ShuffledMessage and routed to OnRecord
  ShuffledMessage msg1 = CreateMessage("run1", 1, 1, "payload1");
  ShuffledMessage msg2 = CreateMessage("run1", 2, 2, "payload2");

  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV(msg1.SerializeAsString()), do_context), IsOk());
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV(msg2.SerializeAsString()), do_context), IsOk());

  ASSERT_THAT(fn.ReceivedRecords(), SizeIs(2));
  EXPECT_THAT(fn.ReceivedRecords()[0].customer_message(), Eq("payload1"));
  EXPECT_THAT(fn.ReceivedRecords()[0].group_id(), Eq(1));
  EXPECT_THAT(fn.ReceivedRecords()[1].customer_message(), Eq("payload2"));
  EXPECT_THAT(fn.ReceivedRecords()[1].group_id(), Eq(2));
}

TEST(FirstRecordConfigDoFnTest, FailsOnEmptyFirstRecordAndRejectsSubsequentRecords) {
  TestFirstRecordDoFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV(""), do_context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());
  EXPECT_TRUE(fn.HasConfigFailure());

  // One-shot semantics: subsequent records are rejected.
  ShuffledMessage msg = CreateMessage("run1", 1, 1, "payload");
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV(msg.SerializeAsString()), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(FirstRecordConfigDoFnTest, FailsOnCorruptedConfigProtoAndRejectsSubsequentRecords) {
  TestFirstRecordDoFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  // Invalid proto wire data
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV("\xff\xff\xff\xff"), do_context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());
  EXPECT_TRUE(fn.HasConfigFailure());

  // One-shot semantics: subsequent records are rejected.
  ShuffledMessage msg = CreateMessage("run1", 1, 1, "payload");
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV(msg.SerializeAsString()), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(FirstRecordConfigDoFnTest, OnConfigFailureRejectsSubsequentRecords) {
  TestFirstRecordDoFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  RunConfig config = CreateValidConfig(101, 202, -5);  // Causes OnConfig to fail
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV(config.SerializeAsString()), do_context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_FALSE(fn.HasConfig());
  EXPECT_TRUE(fn.HasConfigFailure());

  // One-shot semantics: subsequent records (even if valid config) are rejected.
  RunConfig valid_config = CreateValidConfig(101, 202, 50);
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV(valid_config.SerializeAsString()), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(fn.ReceivedRecords(), IsEmpty());
}

TEST(FirstRecordConfigDoFnTest, FailsOnCorruptedRecordProto) {
  TestFirstRecordDoFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  RunConfig config = CreateValidConfig(101, 202, 50);
  ASSERT_THAT(fn.Do(TestFirstRecordDoFn::KV(config.SerializeAsString()), do_context), IsOk());

  // Record with invalid proto wire data fails parsing
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV("\xff\xff\xff\xff"), do_context),
              StatusIs(absl::StatusCode::kInvalidArgument));

  // Valid record afterwards can still be processed
  ShuffledMessage msg = CreateMessage("run1", 1, 1, "payload");
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV(msg.SerializeAsString()), do_context), IsOk());
  ASSERT_THAT(fn.ReceivedRecords(), SizeIs(1));
  EXPECT_THAT(fn.ReceivedRecords()[0].customer_message(), Eq("payload"));
}

TEST(FirstRecordConfigDoFnTest, OnRecordFailureReturnsErrorStatus) {
  TestFirstRecordDoFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  RunConfig config = CreateValidConfig(101, 202, 50);
  ASSERT_THAT(fn.Do(TestFirstRecordDoFn::KV(config.SerializeAsString()), do_context), IsOk());

  // Record that triggers OnRecord error
  ShuffledMessage invalid_msg = CreateMessage("run1", 1, -1, "payload");
  EXPECT_THAT(fn.Do(TestFirstRecordDoFn::KV(invalid_msg.SerializeAsString()), do_context),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

class TestRawBytesFirstRecordDoFn
    : public FirstRecordConfigDoFn<ConfigFrom<RunConfig, Node::kRunContext>,
                                   RecordsFrom<std::string, Node::kClientUploads>> {
 public:
  TestRawBytesFirstRecordDoFn() = default;

  const std::vector<std::string>& ReceivedPayloads() const { return received_payloads_; }

 protected:
  absl::Status OnConfig(const RunConfig& config, DoContext& context) override {
    if (config.target_batch_size() <= 0) {
      return absl::InvalidArgumentError("Invalid target_batch_size");
    }
    return absl::OkStatus();
  }

  absl::Status OnRecord(const std::string& record_data, DoContext& context) override {
    received_payloads_.push_back(record_data);
    return absl::OkStatus();
  }

 private:
  std::vector<std::string> received_payloads_;
};

TEST(FirstRecordConfigDoFnTest, RoutesRawStringRecordsWithoutProtobufParsing) {
  TestRawBytesFirstRecordDoFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  RunConfig config = CreateValidConfig(101, 202, 50);
  ASSERT_THAT(fn.Do(TestRawBytesFirstRecordDoFn::KV(config.SerializeAsString()), do_context),
              IsOk());

  // Raw binary bytes that are not valid protobuf wire format are forwarded directly to OnRecord.
  const std::string raw_checkpoint_bytes = "\xff\xff\xff\xff_non_proto_binary_checkpoint_bytes";
  EXPECT_THAT(fn.Do(TestRawBytesFirstRecordDoFn::KV(raw_checkpoint_bytes), do_context), IsOk());
  ASSERT_THAT(fn.ReceivedPayloads(), SizeIs(1));
  EXPECT_THAT(fn.ReceivedPayloads()[0], Eq(raw_checkpoint_bytes));
}

class TestFinalizableFirstRecordDoFn
    : public FirstRecordConfigDoFn<ConfigFrom<RunConfig, Node::kRunContext>,
                                   RecordsFrom<ShuffledMessage, Node::kClientUploads>> {
 public:
  int OnFinalizeCallCount() const { return on_finalize_call_count_; }

 protected:
  absl::Status OnConfig(const RunConfig& config, DoContext& /*context*/) override {
    if (config.target_batch_size() <= 0) {
      return absl::InvalidArgumentError("Invalid target_batch_size");
    }
    return absl::OkStatus();
  }

  absl::Status OnRecord(const ShuffledMessage& /*record*/, DoContext& /*context*/) override {
    return absl::OkStatus();
  }

  absl::Status OnFinalize(google::protobuf::Any /*config*/, FnContext& /*context*/) override {
    ++on_finalize_call_count_;
    if (!HasConfig()) {
      return absl::FailedPreconditionError("Config was not established before FinalizeReplica.");
    }
    return absl::OkStatus();
  }

 private:
  int on_finalize_call_count_ = 0;
};

TEST(FirstRecordConfigDoFnTest, FinalizeReplicaSucceedsWithoutCallingOnFinalizeWhenDoNeverCalled) {
  TestFinalizableFirstRecordDoFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  EXPECT_FALSE(fn.IsFinalized());
  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), do_context), IsOk());
  EXPECT_TRUE(fn.IsFinalized());
  EXPECT_THAT(fn.OnFinalizeCallCount(), Eq(0));

  // Subsequent FinalizeReplica and Do calls are rejected once finalized.
  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  RunConfig config = CreateValidConfig(101, 202, 50);
  EXPECT_THAT(fn.Do(TestFinalizableFirstRecordDoFn::KV(config.SerializeAsString()), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST(FirstRecordConfigDoFnTest, FinalizeReplicaDelegatesToOnFinalizeWhenConfigFailed) {
  TestFinalizableFirstRecordDoFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  EXPECT_THAT(fn.Do(TestFinalizableFirstRecordDoFn::KV(""), do_context),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_TRUE(fn.HasConfigFailure());

  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(fn.OnFinalizeCallCount(), Eq(1));
  EXPECT_FALSE(fn.IsFinalized());
}

TEST(FirstRecordConfigDoFnTest, FinalizeReplicaDelegatesToOnFinalizeWhenConfigured) {
  TestFinalizableFirstRecordDoFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  RunConfig config = CreateValidConfig(101, 202, 50);
  ASSERT_THAT(fn.Do(TestFinalizableFirstRecordDoFn::KV(config.SerializeAsString()), do_context),
              IsOk());

  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), do_context), IsOk());
  EXPECT_THAT(fn.OnFinalizeCallCount(), Eq(1));
  EXPECT_TRUE(fn.IsFinalized());
}

class TestConfigOnlyDoFn
    : public FirstRecordConfigDoFn<ConfigFrom<RunConfig, Node::kRunContext>, NoRecords> {
 public:
  TestConfigOnlyDoFn() = default;

 protected:
  absl::Status OnConfig(const RunConfig& config, DoContext& context) override {
    if (config.target_batch_size() <= 0) {
      return absl::InvalidArgumentError("Invalid target_batch_size");
    }
    return absl::OkStatus();
  }
};

TEST(FirstRecordConfigDoFnTest, ConfigOnlyDoFnRejectsSubsequentRecords) {
  TestConfigOnlyDoFn fn;
  FakeContext context;
  FakeDoContext do_context(context);

  RunConfig config = CreateValidConfig(101, 202, 50);
  ASSERT_THAT(fn.Do(TestConfigOnlyDoFn::KV(config.SerializeAsString()), do_context), IsOk());
  EXPECT_TRUE(fn.HasConfig());

  // Subsequent record is rejected as config-only session does not accept data records.
  EXPECT_THAT(fn.Do(TestConfigOnlyDoFn::KV("unexpected_record"), do_context),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
