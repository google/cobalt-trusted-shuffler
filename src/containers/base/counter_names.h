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

#ifndef SRC_CONTAINERS_BASE_COUNTER_NAMES_H_
#define SRC_CONTAINERS_BASE_COUNTER_NAMES_H_

#include "absl/strings/string_view.h"

namespace cobalt::trusted_shuffler {

inline constexpr absl::string_view kInputConfigCount = "input_config-count";
inline constexpr absl::string_view kInputConfigInvalid = "input_config-invalid";
inline constexpr absl::string_view kInputRecordCount = "input_record-count";
inline constexpr absl::string_view kInputRecordDropped = "input_record-dropped";
inline constexpr absl::string_view kOversizedPartitionDropped = "oversized_partition-dropped";
inline constexpr absl::string_view kEncryptedOutputRecordCount = "encrypted_output_record-count";
inline constexpr absl::string_view kUnencryptedOutputRecordCount =
    "unencrypted_output_record-count";
inline constexpr absl::string_view kIntegrityFaultCount = "integrity_fault-count";

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_BASE_COUNTER_NAMES_H_
