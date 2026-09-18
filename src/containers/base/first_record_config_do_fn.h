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

#ifndef SRC_CONTAINERS_BASE_FIRST_RECORD_CONFIG_DO_FN_H_
#define SRC_CONTAINERS_BASE_FIRST_RECORD_CONFIG_DO_FN_H_

#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "containers/fns/do_fn.h"
#include "google/protobuf/message.h"
#include "src/containers/base/with_outputs.h"

namespace cobalt::trusted_shuffler {

// Reusable DoFn base class implementing the First-Record Configuration streaming
// pattern.
//
// In confidential streaming pipelines where runtime configuration is passed in-band,
// the configuration proto is written as the initial record (record 0) of the
// session stream.
//
// This base class encapsulates the state machine:
// - Record 0: Parses ConfigProto from input.data, invokes OnConfig(), and transitions
//   to the configured state upon success. If configuration fails, the failure is
//   recorded under one-shot semantics.
// - Records 1..N: Enforces that configuration was successfully established, parses
//   RecordType from input.data (or forwards the raw byte string directly when
//   RecordType is std::string), and routes records to OnRecord(). If configuration
//   failed on record 0, subsequent records are rejected with kFailedPrecondition.
template <typename ConfigProto, typename RecordType = std::string>
class FirstRecordConfigDoFn : public confidential_federated_compute::fns::DoFn {
  static_assert(std::is_base_of_v<google::protobuf::Message, ConfigProto>,
                "ConfigProto must inherit from google::protobuf::Message.");
  static_assert(std::is_base_of_v<google::protobuf::Message, RecordType> ||
                    std::is_same_v<std::string, RecordType>,
                "RecordType must inherit from google::protobuf::Message or be std::string.");

 public:
  // Base class combining FirstRecordConfigDoFn<ConfigProto, RecordType> with
  // the data nodes the container encrypts to and its output declarations.
  template <typename Destinations, typename... OutputStreams>
  class WithOutputs;

  FirstRecordConfigDoFn() = default;

  ~FirstRecordConfigDoFn() override = default;

  // Intercepts input records and executes the configuration state machine.
  // Final to prevent subclasses from inadvertently bypassing configuration checks.
  absl::Status Do(KV input, DoContext& context) final {
    if (config_failure_.has_value()) {
      return absl::FailedPreconditionError(absl::StrCat(
          "Configuration failed on first record; cannot process records: ", *config_failure_));
    }

    if (!config_.has_value()) {
      if (input.data.empty()) {
        config_failure_ = "First record configuration payload must not be empty.";
        return absl::InvalidArgumentError(*config_failure_);
      }

      ConfigProto config;
      if (!config.ParseFromString(input.data)) {
        config_failure_ =
            "Failed to parse first record as configuration proto: corrupted or invalid payload.";
        return absl::InvalidArgumentError(*config_failure_);
      }

      absl::Status config_status = OnConfig(config, context);
      if (!config_status.ok()) {
        config_failure_ = std::string(config_status.message());
        return config_status;
      }

      config_ = std::move(config);
      return absl::OkStatus();
    }

    if constexpr (std::is_same_v<std::string, RecordType>) {
      return OnRecord(input.data, context);
    } else {
      RecordType record;
      if (!record.ParseFromString(input.data)) {
        return absl::InvalidArgumentError(
            "Failed to parse record as record proto: corrupted or invalid payload.");
      }
      return OnRecord(record, context);
    }
  }

  // Returns true if configuration was successfully received, parsed, and validated.
  bool HasConfig() const { return config_.has_value(); }

  // Returns the parsed configuration proto if established, or std::nullopt.
  const std::optional<ConfigProto>& Config() const { return config_; }

 protected:
  // Hook invoked exactly once when the first record arrives. Subclasses must
  // validate the configuration proto.
  virtual absl::Status OnConfig(const ConfigProto& config, DoContext& context) = 0;

  // Hook invoked for all subsequent records (records 1..N) after configuration
  // has succeeded.
  virtual absl::Status OnRecord(const RecordType& record, DoContext& context) = 0;

 private:
  std::optional<ConfigProto> config_;
  std::optional<std::string> config_failure_;
};

template <typename ConfigProto, typename RecordType>
template <typename Destinations, typename... OutputStreams>
class FirstRecordConfigDoFn<ConfigProto, RecordType>::WithOutputs
    : public FirstRecordConfigDoFn<ConfigProto, RecordType>,
      public ::cobalt::trusted_shuffler::WithOutputs<Destinations, OutputStreams...> {
 public:
  using FirstRecordConfigDoFn<ConfigProto, RecordType>::FirstRecordConfigDoFn;
  ~WithOutputs() override = default;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_BASE_FIRST_RECORD_CONFIG_DO_FN_H_
