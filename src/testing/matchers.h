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

#ifndef SRC_TESTING_MATCHERS_H_
#define SRC_TESTING_MATCHERS_H_

#include <type_traits>

#include "gmock/gmock.h"
#include "google/protobuf/util/message_differencer.h"
#include "src/containers/ingest_uploads/upload_parser.h"
#include "src/proto/trusted_shuffler_upload.pb.h"

namespace cobalt::trusted_shuffler::testing {

// Defines a custom EquivToProto matcher for comparing protocol buffers for equivalence.
MATCHER_P(EquivToProto, expected, "") {
  return google::protobuf::util::MessageDifferencer::Equivalent(expected, arg);
}

// Custom GMock matcher for ClientUpload and TrustedShufflerUpload.
MATCHER_P(MatchesUpload, expected, "") {
  if constexpr (std::is_same_v<std::decay_t<decltype(expected)>, TrustedShufflerUpload>) {
    return arg.upload.project_id() == expected.project_id() &&
           arg.upload.metric_id() == expected.metric_id() &&
           arg.upload.serialized_payload() == expected.serialized_payload();
  } else {
    return arg.upload.project_id() == expected.upload.project_id() &&
           arg.upload.metric_id() == expected.upload.metric_id() &&
           arg.upload.serialized_payload() == expected.upload.serialized_payload() &&
           arg.event_time == expected.event_time;
  }
}

}  // namespace cobalt::trusted_shuffler::testing

#endif  // SRC_TESTING_MATCHERS_H_
