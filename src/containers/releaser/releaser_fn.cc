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

#include "src/containers/releaser/releaser_fn.h"

#include <utility>

#include "absl/status/status.h"
#include "src/containers/base/counter_names.h"

namespace cobalt::trusted_shuffler {

absl::Status ReleaserFn::Do(KV input, DoContext& context) {
  context.IncrementCounter(kInputRecordCount);
  context.IncrementCounter(kUnencryptedOutputRecordCount);
  context.EmitUnencrypted(std::move(input));
  return absl::OkStatus();
}

}  // namespace cobalt::trusted_shuffler
