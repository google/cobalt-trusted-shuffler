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

#ifndef SRC_CONTAINERS_MESSAGE_COUNTER_COUNTER_FN_H_
#define SRC_CONTAINERS_MESSAGE_COUNTER_COUNTER_FN_H_

#include <string>

#include "absl/status/status.h"
#include "containers/fns/do_fn.h"
#include "google/protobuf/any.pb.h"

namespace cobalt::trusted_shuffler {

// A DoFn class for counting messages. It keeps track of the number of
// messages it receives and outputs the total count in the finalize step.
class CounterFn : public confidential_federated_compute::fns::DoFn {
 public:
  explicit CounterFn(int key_id) : key_id_(key_id), count_(0L) {}

  ~CounterFn() override = default;

  // Configures the Fn with the counter_name.
  absl::Status InitializeReplica(google::protobuf::Any config,
                                 Context& context) override;

  // Counts the message and forwards it encrypted as a `MESSAGE` output.
  absl::Status Do(KV input, Context& context) override;

  // Emits the final count report as a `REPORT` output.
  absl::Status FinalizeReplica(google::protobuf::Any config,
                               Context& context) override;

 private:
  std::string counter_name_;
  int key_id_;
  int64_t count_;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_MESSAGE_COUNTER_COUNTER_FN_H_