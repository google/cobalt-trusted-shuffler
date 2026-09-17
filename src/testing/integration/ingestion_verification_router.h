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

#ifndef SRC_TESTING_INTEGRATION_INGESTION_VERIFICATION_ROUTER_H_
#define SRC_TESTING_INTEGRATION_INGESTION_VERIFICATION_ROUTER_H_

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "src/testing/integration/records.h"

namespace cobalt::trusted_shuffler::testing::integration {

// Prepends SignedRunContext as Record 0 followed by the partial ingestion reports
// emitted by IngestUploads as Records 1..N for an IngestionVerification session.
absl::StatusOr<KVCollection> RouteToIngestionVerification(
    const EncryptedBlob& signed_run_context, absl::Span<const EncryptedBlob> partial_reports);

// Fault-injection router that places partial ingestion reports before the
// SignedRunContext instead of providing SignedRunContext as Record 0.
absl::StatusOr<KVCollection> RouteToIngestionVerificationWithLateRunContext(
    const EncryptedBlob& signed_run_context, absl::Span<const EncryptedBlob> partial_reports);

// Fault-injection router that injects a PartialIngestionReport with a
// mismatched run_id to trigger an IntegrityFault in IngestionVerification.
absl::StatusOr<KVCollection> RouteToIngestionVerificationWithMismatchedRunId(
    const EncryptedBlob& signed_run_context, absl::Span<const EncryptedBlob> partial_reports);

}  // namespace cobalt::trusted_shuffler::testing::integration

#endif  // SRC_TESTING_INTEGRATION_INGESTION_VERIFICATION_ROUTER_H_
