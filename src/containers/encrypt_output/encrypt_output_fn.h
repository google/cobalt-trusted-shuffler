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

#ifndef SRC_CONTAINERS_ENCRYPT_OUTPUT_ENCRYPT_OUTPUT_FN_H_
#define SRC_CONTAINERS_ENCRYPT_OUTPUT_ENCRYPT_OUTPUT_FN_H_

#include "absl/status/status.h"
#include "containers/fns/do_fn.h"
#include "google/protobuf/any.pb.h"

namespace cobalt::trusted_shuffler {

// DoFn implementation for the output encryption and release fingerprinting stage
// of the Trusted Shuffler.
//
// Currently this is an empty skeleton that does nothing.
// TODO(b/557302528): Implement output encryption and release fingerprinting logic.
class EncryptOutputFn : public confidential_federated_compute::fns::DoFn {
 public:
  EncryptOutputFn() = default;

  ~EncryptOutputFn() override = default;

  // Processes an input KV pair. Currently a no-op that does nothing and emits
  // no outputs.
  absl::Status Do(KV input, Context& context) override;

  // Finalizes replica processing. Currently a no-op that does nothing and emits
  // no outputs.
  // TODO(b/557302528): Package accumulated release fingerprint and worker
  // release key into WorkerKeyAndFingerprint emitted encrypted with key index
  // 0 to the downstream key release verification stage.
  absl::Status FinalizeReplica(google::protobuf::Any config, Context& context) override;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_ENCRYPT_OUTPUT_ENCRYPT_OUTPUT_FN_H_
