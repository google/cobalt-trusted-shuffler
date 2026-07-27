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
#include "src/proto/message_counter/count_report.pb.h"
#include "src/proto/message_counter/counter_config.pb.h"
#include "src/proto/message_counter/output_key.pb.h"

namespace cobalt::trusted_shuffler {

absl::Status CounterFn::InitializeReplica(google::protobuf::Any config, Context& context) {
  CounterConfig counter_config;
  if (!config.UnpackTo(&counter_config)) {
    return absl::InvalidArgumentError("Failed to unpack configuration as CounterConfig");
  }

  if (counter_config.counter_name().empty()) {
    return absl::InvalidArgumentError("No counter_name specified in CounterConfig");
  }
  counter_name_ = counter_config.counter_name();
  return absl::OkStatus();
}

absl::Status CounterFn::Do(KV input, Context& context) {
  count_++;

  OutputKey output_key;
  output_key.set_type(OutputKey::MESSAGE);

  google::protobuf::Any key;
  key.PackFrom(output_key);

  context.EmitEncrypted(key_index_, KV(std::move(key), std::move(input.data)));
  return absl::OkStatus();
}

absl::Status CounterFn::FinalizeReplica(google::protobuf::Any config, Context& context) {
  CountReport report;
  report.set_counter_name(counter_name_);
  report.set_count(count_);

  OutputKey report_key;
  report_key.set_type(OutputKey::REPORT);

  google::protobuf::Any key;
  key.PackFrom(report_key);

  context.EmitUnencrypted(KV(std::move(key), report.SerializeAsString()));

  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler