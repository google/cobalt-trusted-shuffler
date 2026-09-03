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

#ifndef SRC_CONTAINERS_INGESTION_VERIFICATION_INGESTION_VERIFICATION_FN_H_
#define SRC_CONTAINERS_INGESTION_VERIFICATION_INGESTION_VERIFICATION_FN_H_

#include <utility>

#include "absl/status/status.h"
#include "google/protobuf/any.pb.h"
#include "src/containers/base/first_record_config_do_fn.h"
#include "src/proto/config_constraints.pb.h"
#include "src/proto/run_context.pb.h"

namespace cobalt::trusted_shuffler {

// A DoFn class for ingestion verification.
//
// Inherits Do(KV input, Context& context) from FirstRecordConfigDoFn:
// - Record 0: Ingests SignedRunContext via OnConfig(), parses the inner
//   RunContext proto, and logs its contents.
// - Records 1..N: Routes client log records to OnRecord().
class IngestionVerificationFn : public FirstRecordConfigDoFn<SignedRunContext> {
 public:
  explicit IngestionVerificationFn(TrustedShufflerConfigConstraints config_constraints);

  ~IngestionVerificationFn() override = default;

  // Does any setup work needed for this Fn replica.
  // Logs the project id, metric id, and threshold combinations from config constraints.
  absl::Status InitializeReplica(google::protobuf::Any config, Context& context) override;

  const TrustedShufflerConfigConstraints& ConfigConstraints() const { return config_constraints_; }

 protected:
  // Ingests SignedRunContext on record 0, parses the inner RunContext, and prints its contents.
  absl::Status OnConfig(const cobalt::trusted_shuffler::SignedRunContext& signed_run_context,
                        Context& context) override;

  // Ingests incoming client log records.
  absl::Status OnRecord(KV record, Context& context) override;

 private:
  TrustedShufflerConfigConstraints config_constraints_;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_INGESTION_VERIFICATION_INGESTION_VERIFICATION_FN_H_
