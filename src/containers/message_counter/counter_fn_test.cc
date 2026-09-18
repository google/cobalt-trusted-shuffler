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

#include "src/containers/message_counter/counter_fn.h"

#include <string>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/containers/base/counter_names.h"
#include "src/proto/message_counter/count_report.pb.h"
#include "src/proto/message_counter/counter_config.pb.h"
#include "src/proto/message_counter/output_key.pb.h"
#include "src/testing/fake_context.h"

namespace cobalt::trusted_shuffler {
namespace {

static constexpr int kKeyIndex = 5;

using absl_testing::IsOk;
using absl_testing::StatusIs;
using ::cobalt::trusted_shuffler::testing::FakeDoContext;
using ::cobalt::trusted_shuffler::testing::FakeFnContext;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;

TEST(CounterFnTest, InitializeReplicaFailsOnEmptyRequest) {
  testing::FakeContext context;
  CounterFn fn(kKeyIndex);

  EXPECT_THAT(fn.InitializeReplica(google::protobuf::Any(), context),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(CounterFnTest, InitializeReplicaSucceedsWithCounterName) {
  testing::FakeContext context;
  CounterFn fn(kKeyIndex);
  const std::string counter_name = "the_counter_name";

  CounterConfig config;
  config.set_counter_name(counter_name);

  google::protobuf::Any config_any;
  config_any.PackFrom(config);

  EXPECT_THAT(fn.InitializeReplica(config_any, context), IsOk());
}

TEST(CounterFnTest, InitializeReplicaFailsOnMissingCounterName) {
  testing::FakeContext context;
  CounterFn fn(kKeyIndex);

  CounterConfig config;
  // counter_name is not set.

  google::protobuf::Any config_any;
  config_any.PackFrom(config);

  EXPECT_THAT(fn.InitializeReplica(config_any, context),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(CounterFnTest, DoReturnsOk) {
  testing::FakeContext context;
  FakeDoContext do_context(context);
  CounterFn fn(kKeyIndex);

  EXPECT_THAT(fn.Do(CounterFn::KV(), do_context), IsOk());
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kEncryptedOutputRecordCount)), Eq(1));
}

TEST(CounterFnTest, DoEmitsMessages) {
  CounterFn fn(kKeyIndex);

  testing::FakeContext context;
  FakeDoContext do_context(context);

  const std::string message = "a message";
  ASSERT_THAT(fn.Do(CounterFn::KV(google::protobuf::Any(), message), do_context), IsOk());

  ASSERT_THAT(context.Encrypted(), SizeIs(1));

  const std::pair<int, CounterFn::KV> encrypted = context.Encrypted()[0];

  EXPECT_THAT(encrypted.first, Eq(kKeyIndex));

  EXPECT_THAT(encrypted.second.data, Eq(message));

  OutputKey output_key;
  ASSERT_TRUE(encrypted.second.key.UnpackTo(&output_key));
  EXPECT_THAT(output_key.type(), Eq(OutputKey::MESSAGE));

  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  EXPECT_THAT(context.ReadResponses(), IsEmpty());
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kEncryptedOutputRecordCount)), Eq(1));
}

TEST(CounterFnTest, FinalizeReplicaReturnsOk) {
  testing::FakeContext context;
  FakeFnContext fn_context(context);
  CounterFn fn(kKeyIndex);

  EXPECT_THAT(fn.FinalizeReplica(google::protobuf::Any(), fn_context), IsOk());
}

TEST(CounterFnTest, FinalizeReplicaEmitsReport) {
  CounterFn fn(kKeyIndex);

  testing::FakeContext context;
  FakeDoContext do_context(context);
  FakeFnContext fn_context(context);

  const std::string counter_name = "the_counter_name";

  CounterConfig config;
  config.set_counter_name(counter_name);

  google::protobuf::Any config_any;
  config_any.PackFrom(config);

  ASSERT_THAT(fn.InitializeReplica(config_any, context), IsOk());

  int message_count = 10;
  for (int i = 0; i < message_count; i++) {
    ASSERT_THAT(fn.Do(CounterFn::KV(google::protobuf::Any(), "message"), do_context), IsOk());
  }

  ASSERT_THAT(fn.FinalizeReplica(google::protobuf::Any(), fn_context), IsOk());

  EXPECT_THAT(context.Encrypted(), SizeIs(message_count));
  EXPECT_THAT(context.ReadResponses(), IsEmpty());
  EXPECT_THAT(context.GetCounters().at(std::string(kInputRecordCount)), Eq(message_count));
  EXPECT_THAT(context.GetCounters().at(std::string(kEncryptedOutputRecordCount)),
              Eq(message_count));
  EXPECT_THAT(context.GetCounters().at(std::string(kUnencryptedOutputRecordCount)), Eq(1));

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  CounterFn::KV kv = std::move(context.Unencrypted()[0]);

  OutputKey output_key;
  ASSERT_TRUE(kv.key.UnpackTo(&output_key));
  EXPECT_THAT(output_key.type(), Eq(OutputKey::REPORT));

  CountReport count_report;
  ASSERT_TRUE(count_report.ParseFromString(kv.data));
  EXPECT_THAT(count_report.counter_name(), Eq(counter_name));
  EXPECT_THAT(count_report.count(), Eq(message_count));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler