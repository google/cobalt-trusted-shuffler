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

#include "src/containers/message_counter/counter_session.h"

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/containers/message_counter/proto/count_report.pb.h"
#include "src/containers/message_counter/proto/counter_config.pb.h"
#include "src/containers/message_counter/proto/output_key.pb.h"
#include "src/testing/fake_context.h"

namespace cobalt::trusted_shuffler {
namespace {

static constexpr int kKeyId = 1;

using absl_testing::IsOk;
using absl_testing::StatusIs;
using fcp::confidentialcompute::BlobMetadata;
using fcp::confidentialcompute::CommitRequest;
using fcp::confidentialcompute::CommitResponse;
using fcp::confidentialcompute::ConfigureRequest;
using fcp::confidentialcompute::ConfigureResponse;
using fcp::confidentialcompute::FinalizeRequest;
using fcp::confidentialcompute::FinalizeResponse;
using fcp::confidentialcompute::WriteFinishedResponse;
using fcp::confidentialcompute::WriteRequest;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;

TEST(CounterSessionTest, ConfigureFailsOnEmptyRequest) {
  testing::FakeContext context;
  CounterSession session(kKeyId);

  const absl::StatusOr<ConfigureResponse> response = session.Configure(ConfigureRequest(), context);

  EXPECT_THAT(response, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(CounterSessionTest, ConfigureSucceedsWithCounterName) {
  testing::FakeContext context;
  CounterSession session(kKeyId);
  const std::string counter_name = "the_counter_name";

  CounterConfig config;
  config.set_counter_name(counter_name);

  ConfigureRequest config_request;
  config_request.mutable_configuration()->PackFrom(config);

  EXPECT_THAT(session.Configure(config_request, context), IsOk());
}

TEST(CounterSessionTest, WriteReturnsOk) {
  testing::FakeContext context;
  CounterSession session(kKeyId);

  EXPECT_THAT(session.Write(WriteRequest(), "", context), IsOk());
}

TEST(CounterSessionTest, WriteWritesMessages) {
  CounterSession session(kKeyId);

  testing::FakeContext context;

  const std::string message = "a message";
  ASSERT_THAT(session.Write(WriteRequest(), message, context), IsOk());

  ASSERT_THAT(context.Encrypted(), SizeIs(1));

  const std::pair<int, CounterSession::KV> encrypted = context.Encrypted()[0];

  EXPECT_THAT(encrypted.first, Eq(kKeyId));

  EXPECT_THAT(encrypted.second.data, Eq(message));

  OutputKey output_key;
  ASSERT_TRUE(encrypted.second.key.UnpackTo(&output_key));
  EXPECT_THAT(output_key.type(), Eq(OutputKey::MESSAGE));

  EXPECT_THAT(context.Unencrypted(), IsEmpty());
  EXPECT_THAT(context.ReadResponses(), IsEmpty());
}

TEST(CounterSessionTest, CommitReturnsOk) {
  testing::FakeContext context;
  CounterSession session(kKeyId);

  EXPECT_THAT(session.Commit(CommitRequest(), context), IsOk());
}

TEST(CounterSessionTest, FinalizeReturnsOk) {
  testing::FakeContext context;
  CounterSession session(kKeyId);

  EXPECT_THAT(session.Finalize(FinalizeRequest(), BlobMetadata(), context), IsOk());
}

TEST(CounterSessionTest, FinalizeEmitsReport) {
  CounterSession session(kKeyId);

  testing::FakeContext context;

  const std::string counter_name = "the_counter_name";

  CounterConfig config;
  config.set_counter_name(counter_name);

  ConfigureRequest config_request;
  config_request.mutable_configuration()->PackFrom(config);

  ASSERT_THAT(session.Configure(config_request, context), IsOk());

  int message_count = 10;
  for (int i = 0; i < message_count; i++) {
    ASSERT_THAT(session.Write(WriteRequest(), "message", context), IsOk());
  }

  ASSERT_THAT(session.Finalize(FinalizeRequest(), BlobMetadata(), context), IsOk());

  EXPECT_THAT(context.Encrypted(), SizeIs(message_count));
  EXPECT_THAT(context.ReadResponses(), IsEmpty());

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  CounterSession::KV kv = std::move(context.Unencrypted()[0]);

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