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

#include "src/testing/integration/ingestion_verification_router.h"

#include <vector>

#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/proto/ingestion.pb.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {
namespace {

using ::absl_testing::IsOk;
using ::testing::SizeIs;

TEST(IngestionVerificationRouterTest, PrependsSignedRunContextAndPartialReports) {
  const EncryptedBlob signed_run_context("ctx_ciphertext");
  const std::vector<EncryptedBlob> reports = {
      EncryptedBlob("report_1"),
      EncryptedBlob("report_2"),
  };

  const absl::StatusOr<KVCollection> result =
      RouteToIngestionVerification(signed_run_context, reports);
  ASSERT_THAT(result.status(), IsOk());
  ASSERT_THAT(*result, SizeIs(3));
  EXPECT_EQ((*result)[0].data, "ctx_ciphertext");
  EXPECT_EQ((*result)[1].data, "report_1");
  EXPECT_EQ((*result)[2].data, "report_2");
}

TEST(IngestionVerificationRouterTest, RouteWithLateRunContextPlacesRunContextLast) {
  const EncryptedBlob signed_run_context("ctx_ciphertext");
  const std::vector<EncryptedBlob> reports = {
      EncryptedBlob("report_1"),
      EncryptedBlob("report_2"),
  };

  const absl::StatusOr<KVCollection> result =
      RouteToIngestionVerificationWithLateRunContext(signed_run_context, reports);
  ASSERT_THAT(result.status(), IsOk());
  ASSERT_THAT(*result, SizeIs(3));
  EXPECT_EQ((*result)[0].data, "report_1");
  EXPECT_EQ((*result)[1].data, "report_2");
  EXPECT_EQ((*result)[2].data, "ctx_ciphertext");
}

TEST(IngestionVerificationRouterTest, RouteWithMismatchedRunIdAppendsTamperedReport) {
  const EncryptedBlob signed_run_context("ctx_ciphertext");
  const std::vector<EncryptedBlob> reports = {
      EncryptedBlob("report_1"),
  };

  const absl::StatusOr<KVCollection> result =
      RouteToIngestionVerificationWithMismatchedRunId(signed_run_context, reports);
  ASSERT_THAT(result.status(), IsOk());
  ASSERT_THAT(*result, SizeIs(3));
  EXPECT_EQ((*result)[0].data, "ctx_ciphertext");
  EXPECT_EQ((*result)[1].data, "report_1");

  PartialIngestionReport tampered_report;
  ASSERT_TRUE(tampered_report.ParseFromString((*result)[2].data));
  EXPECT_EQ(tampered_report.partial_fingerprint().run_id(), "mismatched_run_id");
}

}  // namespace
}  // namespace cobalt::trusted_shuffler::testing::integration
