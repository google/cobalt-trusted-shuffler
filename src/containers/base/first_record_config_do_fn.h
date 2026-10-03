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

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "containers/fns/do_fn.h"
#include "google/protobuf/message.h"
#include "src/containers/base/pipeline_nodes.h"
#include "src/containers/base/with_outputs.h"

namespace cobalt::trusted_shuffler {

// Declares the configuration proto a FirstRecordConfigDoFn reads from record 0
// and the data node it is read from.
template <typename ConfigProto, Node Source>
struct ConfigFrom {
  static_assert(internal::kAreDataNodes<Source>,
                "Node::kNone is not a data node and cannot be read from.");

  // The configuration proto parsed from record 0.
  using Value = ConfigProto;

  // The data node this configuration is read from.
  inline static constexpr Node kSource = Source;
};

// Declares the records a FirstRecordConfigDoFn reads after its configuration
// and the data nodes they are read from.
//
// The nodes here must be listed once each and in ascending order. A container
// that accepts no records after its configuration lists no nodes.
template <typename RecordType, Node... Sources>
struct RecordsFrom {
  static_assert(internal::kAreDataNodes<Sources...>,
                "Node::kNone is not a data node and cannot be read from.");
  static_assert(internal::kAreAscending<Sources...>,
                "The data nodes a container reads records from must be listed once each and in "
                "ascending order.");
  static_assert(!std::is_void_v<RecordType> || sizeof...(Sources) == 0,
                "A config-only container (RecordType is void) cannot declare record sources.");

  // The record type parsed from records 1..N.
  using Value = RecordType;

  // The data nodes this container reads records from, in ascending order.
  inline static constexpr std::array<Node, sizeof...(Sources)> kSources = {Sources...};
};

// Declares that a container accepts no records after its configuration.
using NoRecords = RecordsFrom<void>;

namespace internal {

// Whether `T` is a ConfigFrom<...> declaration.
template <typename T>
inline constexpr bool kIsConfigFrom = false;
template <typename ConfigProto, Node Source>
inline constexpr bool kIsConfigFrom<ConfigFrom<ConfigProto, Source>> = true;

// Whether `T` is a RecordsFrom<...> declaration.
template <typename T>
inline constexpr bool kIsRecordsFrom = false;
template <typename RecordType, Node... Sources>
inline constexpr bool kIsRecordsFrom<RecordsFrom<RecordType, Sources...>> = true;

// Whether a container reads its configuration from a different data node than
// any of its records.
template <Node ConfigSource, Node... RecordSources>
inline constexpr bool kAreDisjointSources = ((RecordSources != ConfigSource) && ...);

// The WithInputs mixin of a FirstRecordConfigDoFn, listing the data node its
// configuration is read from and the data nodes its records are read from
// together in ascending order.
//
// A malformed declaration yields WithInputs<>, so that it is reported by the
// assertions in FirstRecordConfigDoFn alone.
template <typename ConfigInput, typename RecordInput>
struct FirstRecordInputs {
  using Type = WithInputs<>;
};

template <typename ConfigProto, Node ConfigSource, typename RecordType, Node... RecordSources>
struct FirstRecordInputs<ConfigFrom<ConfigProto, ConfigSource>,
                         RecordsFrom<RecordType, RecordSources...>> {
  static_assert(kAreDisjointSources<ConfigSource, RecordSources...>,
                "A container cannot read its configuration and its records from the same data "
                "node.");

  static constexpr std::array<Node, 1 + sizeof...(RecordSources)> kSortedSources = [] {
    std::array<Node, 1 + sizeof...(RecordSources)> sources = {ConfigSource, RecordSources...};
    std::sort(sources.begin(), sources.end());
    return sources;
  }();

  template <size_t... Indices>
  static WithInputs<kSortedSources[Indices]...> Build(std::index_sequence<Indices...>);

  using Type =
      std::conditional_t<kAreDisjointSources<ConfigSource, RecordSources...>,
                         decltype(Build(std::make_index_sequence<kSortedSources.size()>())),
                         WithInputs<>>;
};

// Base class defining OnRecord() based on whether RecordType is void.
template <typename RecordType>
class RecordHandler {
 protected:
  virtual ~RecordHandler() = default;

  // Hook invoked for all subsequent records (records 1..N) after configuration
  // has succeeded.
  virtual absl::Status OnRecord(const RecordType& record,
                                confidential_federated_compute::fns::DoFn::DoContext& context) = 0;
};

template <>
class RecordHandler<void> {
 protected:
  virtual ~RecordHandler() = default;

  // Hook invoked if a record arrives after configuration in a config-only session.
  // Defaults to returning FailedPreconditionError. Subclasses (such as PrePaddingFn)
  // may override this to signal an integrity fault.
  virtual absl::Status OnRecord(confidential_federated_compute::fns::DoFn::DoContext& /*context*/) {
    return absl::FailedPreconditionError(
        "This session is config-only and does not accept data records.");
  }
};

}  // namespace internal

// Reusable DoFn base class implementing the First-Record Configuration streaming
// pattern.
//
// In confidential streaming pipelines where runtime configuration is passed in-band,
// the configuration proto is written as the initial record (record 0) of the
// session stream.
//
// A container declares its configuration proto and record type together with
// the data nodes each is read from, as ConfigFrom<ConfigProto, Node> and
// RecordsFrom<RecordType, Node...>.
//
// This base class encapsulates the state machine:
// - Record 0: Parses ConfigProto from input.data, invokes OnConfig(), and transitions
//   to the configured state upon success. If configuration fails, the failure is
//   recorded under one-shot semantics.
// - Records 1..N: Enforces that configuration was successfully established, parses
//   RecordType from input.data (or forwards the raw byte string directly when
//   RecordType is std::string), and routes records to OnRecord(). If configuration
//   failed on record 0, subsequent records are rejected with kFailedPrecondition.
template <typename ConfigInput, typename RecordInput>
class FirstRecordConfigDoFn : public confidential_federated_compute::fns::DoFn,
                              public internal::RecordHandler<typename RecordInput::Value> {
  static_assert(internal::kIsConfigFrom<ConfigInput>,
                "A container must declare its configuration as ConfigFrom<ConfigProto, Node>, "
                "naming the data node it is read from.");
  static_assert(internal::kIsRecordsFrom<RecordInput>,
                "A container must declare its records as RecordsFrom<RecordType, Node...>, naming "
                "the data nodes they are read from.");

  // The configuration proto parsed from record 0.
  using ConfigProto = typename ConfigInput::Value;

  // The record type parsed from records 1..N.
  using RecordType = typename RecordInput::Value;

  static_assert(std::is_base_of_v<google::protobuf::Message, ConfigProto>,
                "ConfigProto must inherit from google::protobuf::Message.");
  static_assert(
      std::is_base_of_v<google::protobuf::Message, RecordType> ||
          std::is_same_v<std::string, RecordType> || std::is_void_v<RecordType>,
      "RecordType must inherit from google::protobuf::Message, be std::string, or be void.");

 public:
  // The data node the container reads its configuration proto from on record 0.
  inline static constexpr Node kConfigSource = ConfigInput::kSource;

  // The data nodes the container reads records from on records 1..N, in ascending order.
  inline static constexpr auto kRecordSources = RecordInput::kSources;

  // Base class combining FirstRecordConfigDoFn<ConfigInput, RecordInput> with
  // the data nodes the container reads from and encrypts to and its output
  // declarations.
  template <typename Destinations, typename... OutputStreams>
  class WithOutputs;

  FirstRecordConfigDoFn() = default;

  ~FirstRecordConfigDoFn() override = default;

  // Intercepts input records and executes the configuration state machine.
  // Final to prevent subclasses from inadvertently bypassing configuration checks.
  absl::Status Do(KV input, DoContext& context) final {
    if (is_finalized_) {
      return absl::FailedPreconditionError("Replica is already finalized.");
    }
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

    if constexpr (std::is_void_v<RecordType>) {
      return this->OnRecord(context);
    } else if constexpr (std::is_same_v<std::string, RecordType>) {
      return this->OnRecord(input.data, context);
    } else {
      RecordType record;
      if (!record.ParseFromString(input.data)) {
        return absl::InvalidArgumentError(
            "Failed to parse record as record proto: corrupted or invalid payload.");
      }
      return this->OnRecord(record, context);
    }
  }

  // Intercepts replica finalization and returns OkStatus when Do() was never called.
  // Final to prevent subclasses from inadvertently bypassing unconfigured replica checks.
  absl::Status FinalizeReplica(google::protobuf::Any config, FnContext& context) final {
    if (is_finalized_) {
      return absl::FailedPreconditionError("Replica is already finalized.");
    }
    if (!HasConfig() && !HasConfigFailure()) {
      is_finalized_ = true;
      return absl::OkStatus();
    }
    absl::Status status = OnFinalize(std::move(config), context);
    if (status.ok()) {
      is_finalized_ = true;
    }
    return status;
  }

  // Returns true if configuration was successfully received, parsed, and validated.
  bool HasConfig() const { return config_.has_value(); }

  // Returns true if the first record arrived and failed to parse or validate as
  // configuration.
  bool HasConfigFailure() const { return config_failure_.has_value(); }

  // Returns true if the replica has been successfully finalized.
  bool IsFinalized() const { return is_finalized_; }

  // Returns the parsed configuration proto if established, or std::nullopt.
  const std::optional<ConfigProto>& Config() const { return config_; }

 protected:
  // Hook invoked exactly once when the first record arrives. Subclasses must
  // validate the configuration proto.
  virtual absl::Status OnConfig(const ConfigProto& config, DoContext& context) = 0;

  // Hook invoked during FinalizeReplica when Do() was called at least once.
  virtual absl::Status OnFinalize(google::protobuf::Any config, FnContext& context) {
    return absl::OkStatus();
  }

 private:
  std::optional<ConfigProto> config_;
  std::optional<std::string> config_failure_;
  bool is_finalized_ = false;
};

template <typename ConfigInput, typename RecordInput>
template <typename Destinations, typename... OutputStreams>
class FirstRecordConfigDoFn<ConfigInput, RecordInput>::WithOutputs
    : public FirstRecordConfigDoFn<ConfigInput, RecordInput>,
      public internal::FirstRecordInputs<ConfigInput, RecordInput>::Type,
      public ::cobalt::trusted_shuffler::WithOutputs<Destinations, OutputStreams...> {
 public:
  using FirstRecordConfigDoFn<ConfigInput, RecordInput>::FirstRecordConfigDoFn;
  ~WithOutputs() override = default;

  // The data node the container reads its configuration proto from on record 0.
  inline static constexpr Node kConfigSource =
      FirstRecordConfigDoFn<ConfigInput, RecordInput>::kConfigSource;

  // The data nodes the container reads records from on records 1..N, in ascending order.
  inline static constexpr auto kRecordSources =
      FirstRecordConfigDoFn<ConfigInput, RecordInput>::kRecordSources;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_BASE_FIRST_RECORD_CONFIG_DO_FN_H_
