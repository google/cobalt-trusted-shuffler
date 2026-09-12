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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/proto/common.pb.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/release.pb.h"
#include "src/proto/run_context.pb.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::testing::HasSubstr;
using ::testing::Not;

TEST(DebugRedactTest, RunContextRedactsSensitiveFields) {
  RunContext run_context;
  run_context.set_run_id("sensitive_run_id_value_12345");
  run_context.set_run_hash_key("sensitive_hash_key_value_67890");
  run_context.set_release_key("sensitive_release_key_value_abcde");
  run_context.mutable_run_config()->set_target_batch_size(100);

  const std::string debug_str = run_context.DebugString();
  EXPECT_THAT(debug_str, Not(HasSubstr("sensitive_run_id_value_12345")));
  EXPECT_THAT(debug_str, Not(HasSubstr("sensitive_hash_key_value_67890")));
  EXPECT_THAT(debug_str, Not(HasSubstr("sensitive_release_key_value_abcde")));
  EXPECT_THAT(debug_str, HasSubstr("target_batch_size: 100"));
  EXPECT_THAT(debug_str, HasSubstr("[REDACTED]"));

  const std::string short_debug_str = run_context.ShortDebugString();
  EXPECT_THAT(short_debug_str, Not(HasSubstr("sensitive_run_id_value_12345")));
  EXPECT_THAT(short_debug_str, Not(HasSubstr("sensitive_hash_key_value_67890")));
  EXPECT_THAT(short_debug_str, Not(HasSubstr("sensitive_release_key_value_abcde")));
  EXPECT_THAT(short_debug_str, HasSubstr("target_batch_size: 100"));
  EXPECT_THAT(short_debug_str, HasSubstr("[REDACTED]"));
}

TEST(DebugRedactTest, ShuffledMessageRedactsSensitiveFields) {
  ShuffledMessage message;
  message.set_run_id("sensitive_run_id_in_shuffled_message");
  message.set_is_padding(true);
  message.set_customer_message("sensitive_customer_payload_data");
  message.set_shuffle_count(2);

  const std::string debug_str = message.DebugString();
  EXPECT_THAT(debug_str, Not(HasSubstr("sensitive_run_id_in_shuffled_message")));
  EXPECT_THAT(debug_str, Not(HasSubstr("sensitive_customer_payload_data")));
  EXPECT_THAT(debug_str, HasSubstr("shuffle_count: 2"));
  EXPECT_THAT(debug_str, HasSubstr("[REDACTED]"));

  const std::string short_debug_str = message.ShortDebugString();
  EXPECT_THAT(short_debug_str, Not(HasSubstr("sensitive_run_id_in_shuffled_message")));
  EXPECT_THAT(short_debug_str, Not(HasSubstr("sensitive_customer_payload_data")));
  EXPECT_THAT(short_debug_str, HasSubstr("shuffle_count: 2"));
  EXPECT_THAT(short_debug_str, HasSubstr("[REDACTED]"));
}

TEST(DebugRedactTest, RunFingerprintRedactsRunId) {
  RunFingerprint fingerprint;
  fingerprint.set_run_id("sensitive_run_id_in_fingerprint");
  fingerprint.set_stage_id(Stage::STAGE_INGESTION);

  const std::string debug_str = fingerprint.DebugString();
  EXPECT_THAT(debug_str, Not(HasSubstr("sensitive_run_id_in_fingerprint")));
  EXPECT_THAT(debug_str, HasSubstr("[REDACTED]"));

  const std::string short_debug_str = fingerprint.ShortDebugString();
  EXPECT_THAT(short_debug_str, Not(HasSubstr("sensitive_run_id_in_fingerprint")));
  EXPECT_THAT(short_debug_str, HasSubstr("[REDACTED]"));
}

TEST(DebugRedactTest, ThresholdReportRedactsRunId) {
  ThresholdReport report;
  report.set_run_id("sensitive_run_id_in_threshold_report");

  const std::string debug_str = report.DebugString();
  EXPECT_THAT(debug_str, Not(HasSubstr("sensitive_run_id_in_threshold_report")));
  EXPECT_THAT(debug_str, HasSubstr("[REDACTED]"));

  const std::string short_debug_str = report.ShortDebugString();
  EXPECT_THAT(short_debug_str, Not(HasSubstr("sensitive_run_id_in_threshold_report")));
  EXPECT_THAT(short_debug_str, HasSubstr("[REDACTED]"));
}

TEST(DebugRedactTest, ReleasedKeyReportRedactsReleaseKey) {
  ReleasedKeyReport report;
  report.set_release_key("sensitive_release_key_in_report");

  const std::string debug_str = report.DebugString();
  EXPECT_THAT(debug_str, Not(HasSubstr("sensitive_release_key_in_report")));
  EXPECT_THAT(debug_str, HasSubstr("[REDACTED]"));

  const std::string short_debug_str = report.ShortDebugString();
  EXPECT_THAT(short_debug_str, Not(HasSubstr("sensitive_release_key_in_report")));
  EXPECT_THAT(short_debug_str, HasSubstr("[REDACTED]"));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
