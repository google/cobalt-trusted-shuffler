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

#include <string>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "containers/session.h"
#include "src/proto/fingerprint.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

absl::StatusOr<KVCollection> RouteToIngestionVerification(
    const EncryptedBlob& signed_run_context, absl::Span<const EncryptedBlob> partial_reports) {
  KVCollection inputs;
  inputs.reserve(1 + partial_reports.size());
  inputs.push_back(signed_run_context.ToKV());

  for (const EncryptedBlob& report : partial_reports) {
    inputs.push_back(report.ToKV());
  }

  return inputs;
}

absl::StatusOr<KVCollection> RouteToIngestionVerificationWithLateRunContext(
    const EncryptedBlob& signed_run_context, absl::Span<const EncryptedBlob> partial_reports) {
  KVCollection inputs;
  inputs.reserve(1 + partial_reports.size());
  for (const EncryptedBlob& report : partial_reports) {
    inputs.push_back(report.ToKV());
  }
  inputs.push_back(signed_run_context.ToKV());
  return inputs;
}

absl::StatusOr<KVCollection> RouteToIngestionVerificationWithMismatchedRunId(
    const EncryptedBlob& signed_run_context, absl::Span<const EncryptedBlob> partial_reports) {
  KVCollection inputs;
  inputs.reserve(2 + partial_reports.size());
  inputs.push_back(signed_run_context.ToKV());

  for (const EncryptedBlob& report : partial_reports) {
    inputs.push_back(report.ToKV());
  }

  PartialIngestionReport tampered_report;
  tampered_report.mutable_partial_fingerprint()->set_run_id("mismatched_run_id");
  tampered_report.mutable_partial_fingerprint()->set_stage_id(Stage::STAGE_INGESTION);
  tampered_report.mutable_partial_fingerprint()->set_is_partial(true);
  inputs.push_back(EncryptedBlob(tampered_report.SerializeAsString()).ToKV());

  return inputs;
}

}  // namespace cobalt::trusted_shuffler::testing::integration
