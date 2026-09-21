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

#ifndef SRC_CONTAINERS_BASE_INTEGRITY_FAULT_H_
#define SRC_CONTAINERS_BASE_INTEGRITY_FAULT_H_

#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "containers/fns/fn.h"
#include "src/containers/base/counter_names.h"
#include "src/containers/base/with_outputs.h"
#include "src/proto/common.pb.h"

namespace cobalt::trusted_shuffler {

// Encapsulates the result of signaling an integrity fault.
class IntegrityFaultError {
 public:
  enum class Error {
    kNone,
    kAlreadyEmitted,
    kFailedEmission,
  };

  IntegrityFaultError(Error error, std::string message)
      : error_(error), message_(std::move(message)) {}

  Error error() const { return error_; }
  const std::string& message() const { return message_; }

  bool ok() const { return error_ == Error::kNone; }

  // Converts the fault result into an absl::Status suitable for aborting
  // container transform execution (e.g. from OnRecord, Do, or OnConfig).
  absl::Status ToStatus() const {
    if (error_ == Error::kFailedEmission) {
      return absl::InternalError(message_);
    }
    return absl::FailedPreconditionError(message_);
  }

  bool operator==(const IntegrityFaultError& other) const = default;

 private:
  Error error_;
  std::string message_;
};

// Tracks integrity faults within a container replica and handles formatting,
// unencrypted record emission via WithOutputs, and error logging for integrity
// violations.
//
// In Trusted Shuffler pipelines, each container stage emits unencrypted fault records tagged with
// that container's specific output header protobuf (e.g., ReleaseOutputHeader with
// TYPE_INTEGRITY_FAULT for encrypt_output, ScatterShuffleHeader with mutable_integrity_fault()
// for scatter_shuffle, IngestMessagesHeader for ingest_uploads, or IntegrityFault for stages
// without dedicated header protos like threshold_filter).
//
// Single-Emission Invariant:
// When SignalFault is invoked for the first time, it sets HasFault() = true,
// FaultEmitted() = true, emits an unencrypted IntegrityFault record via container.EmitUnencrypted,
// logs via LOG(ERROR), and returns an IntegrityFaultError result.
//
// Subsequent invocations return an IntegrityFaultError with Error::kAlreadyEmitted immediately
// without re-emitting unencrypted records.
template <typename HeaderType = IntegrityFault>
class IntegrityFaultHandler {
 public:
  // Constructs handler with a default-constructed HeaderType (e.g. IntegrityFault).
  IntegrityFaultHandler() = default;

  // Constructs handler with a custom container protobuf header key (e.g. ReleaseOutputHeader,
  // ScatterShuffleHeader).
  explicit IntegrityFaultHandler(HeaderType header) : header_(std::move(header)) {}

  bool HasFault() const { return has_fault_; }
  bool FaultEmitted() const { return fault_emitted_; }

  // Signals an integrity fault:
  // - Formats the reason (or uses "IntegrityFault" when reason is empty).
  // - Emits an unencrypted record containing a serialized IntegrityFault message payload
  //   keyed by the configured header protobuf at most once via container.EmitUnencrypted.
  // - Increments kUnencryptedOutputRecordCount and kIntegrityFaultCount.
  // - Returns IntegrityFaultError, which can be converted to absl::Status via ToStatus()
  //   to abort transform execution (e.g. `return fault_handler_.SignalFault(...).ToStatus();`).
  template <typename Container>
  IntegrityFaultError SignalFault(absl::string_view reason, const Container& container,
                                  confidential_federated_compute::fns::Fn::FnContext& context) {
    has_fault_ = true;

    if (fault_emitted_) {
      return IntegrityFaultError(IntegrityFaultError::Error::kAlreadyEmitted,
                                 "Integrity fault already emitted.");
    }

    fault_emitted_ = true;
    const std::string formatted_message =
        reason.empty() ? "IntegrityFault" : absl::StrCat("IntegrityFault: ", reason);
    LOG(ERROR) << formatted_message;

    IntegrityFault fault;
    context.IncrementCounter(kUnencryptedOutputRecordCount);
    context.IncrementCounter(kIntegrityFaultCount);
    absl::Status emit_status = container.EmitUnencrypted(context, header_, fault);
    if (!emit_status.ok()) {
      LOG(WARNING) << "Failed to emit integrity fault record: " << emit_status;
      return IntegrityFaultError(IntegrityFaultError::Error::kFailedEmission,
                                 std::string(emit_status.message()));
    }

    return IntegrityFaultError(IntegrityFaultError::Error::kNone, formatted_message);
  }

  template <typename Container>
  IntegrityFaultError SignalFault(const Container& container,
                                  confidential_federated_compute::fns::Fn::FnContext& context) {
    return SignalFault(/*reason=*/"", container, context);
  }

 private:
  HeaderType header_;
  bool has_fault_ = false;
  bool fault_emitted_ = false;
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_BASE_INTEGRITY_FAULT_H_
