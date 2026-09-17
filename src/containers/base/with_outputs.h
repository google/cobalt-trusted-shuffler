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

#ifndef SRC_CONTAINERS_BASE_WITH_OUTPUTS_H_
#define SRC_CONTAINERS_BASE_WITH_OUTPUTS_H_

#include <cstddef>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "containers/fns/do_fn.h"
#include "containers/fns/fn.h"
#include "containers/session.h"
#include "google/protobuf/any.pb.h"
#include "google/protobuf/message.h"

namespace cobalt::trusted_shuffler {

// Key type of an output stream that leaves KV.key empty.
struct NoKey {};

// Whether an output stream's payload is readable by the untrusted orchestrator.
enum class Visibility {
  // KV.data is encrypted under the stream's key index and is opaque to the
  // untrusted orchestrator. Only a downstream TEE stage holding the matching
  // re-encryption key can read it.
  kEncrypted,

  // KV.data is plaintext and fully visible to the untrusted orchestrator.
  kUnencrypted,
};

// Key index of an unencrypted stream, whose payload is never re-encrypted.
inline constexpr int kNoKeyIndex = -1;

// A single output stream that a container is allowed to emit on.
//
// Containers declare their streams using the Encrypted, EncryptedKV,
// Unencrypted, and UnencryptedKV aliases below rather than naming OutputStream
// directly. The declared list drives Emit*() dispatch: the (visibility, value
// type) pair selects the stream, and the stream supplies the destination
// re-encryption key index.
//
// KV.key is always plaintext and visible to the untrusted orchestrator, on both
// encrypted and unencrypted streams.
template <Visibility StreamVisibility, typename KeyType, typename ValueType, int KeyIndex>
struct OutputStream {
  static_assert(std::is_same_v<KeyType, NoKey> ||
                    std::is_base_of_v<google::protobuf::Message, KeyType>,
                "An output stream's key must be a Protobuf message.");
  // TODO(b/563350645): Remove the std::string and Session::KV exceptions once
  // the legacy message_counter and releaser containers are deleted.
  static_assert(std::is_base_of_v<google::protobuf::Message, ValueType> ||
                    std::is_same_v<ValueType, std::string> ||
                    (StreamVisibility == Visibility::kUnencrypted &&
                     std::is_same_v<KeyType, NoKey> &&
                     std::is_same_v<ValueType, confidential_federated_compute::Session::KV>),
                "An output stream's value must be a Protobuf message.");
  static_assert(StreamVisibility == Visibility::kUnencrypted ? KeyIndex == kNoKeyIndex
                                                             : KeyIndex >= 0,
                "An encrypted output stream needs a non-negative re-encryption key index, and "
                "an unencrypted one must not declare an index.");

  // Whether the payload is encrypted for a downstream TEE stage.
  inline static constexpr Visibility kVisibility = StreamVisibility;

  // The destination re-encryption key index, or kNoKeyIndex when unencrypted.
  inline static constexpr int kKeyIndex = KeyIndex;

  // The plaintext routing header packed into KV.key, or NoKey to leave it
  // empty. A key of google::protobuf::Any is forwarded to KV.key unchanged
  // instead of being nested inside another Any.
  using Key = KeyType;

  // The payload serialized into KV.data.
  using Value = ValueType;
};

// Declares an encrypted value stream emitted under KeyIndex with an empty key.
template <typename ValueType, int KeyIndex>
using Encrypted = OutputStream<Visibility::kEncrypted, NoKey, ValueType, KeyIndex>;

// Declares an encrypted key-value stream where KeyType is packed into KV.key in
// plaintext for orchestrator routing and ValueType is encrypted under KeyIndex.
template <typename KeyType, typename ValueType, int KeyIndex>
using EncryptedKV = OutputStream<Visibility::kEncrypted, KeyType, ValueType, KeyIndex>;

// Declares a plaintext value stream emitted with an empty key.
template <typename ValueType>
using Unencrypted = OutputStream<Visibility::kUnencrypted, NoKey, ValueType, kNoKeyIndex>;

// Declares a plaintext key-value stream where both KeyType and ValueType are
// visible to the untrusted orchestrator.
template <typename KeyType, typename ValueType>
using UnencryptedKV = OutputStream<Visibility::kUnencrypted, KeyType, ValueType, kNoKeyIndex>;

namespace internal {

// Sentinel position returned by FindOutputStream() when zero or multiple
// declared streams match.
inline constexpr int kNoMatchingStream = -1;

// Whether `Stream` is the stream an Emit*() call for `ValueArg` refers to.
// Streams are selected on visibility and value type alone; the key type is
// checked separately so that a key mismatch reports itself rather than looking
// like an undeclared stream.
template <typename Stream, Visibility StreamVisibility, typename ValueArg>
inline constexpr bool kSelectsStream =
    Stream::kVisibility == StreamVisibility &&
    std::is_same_v<typename Stream::Value, std::decay_t<ValueArg>>;

// Returns the position of the unique declared stream matching
// `StreamVisibility` and `ValueArg`, or kNoMatchingStream if there is no such
// stream or more than one.
template <Visibility StreamVisibility, typename ValueArg, typename... Streams>
constexpr int FindOutputStream() {
  constexpr bool kSelected[] = {kSelectsStream<Streams, StreamVisibility, ValueArg>...};
  int found = kNoMatchingStream;
  for (size_t i = 0; i < sizeof...(Streams); ++i) {
    if (!kSelected[i]) {
      continue;
    }
    if (found != kNoMatchingStream) {
      return kNoMatchingStream;
    }
    found = static_cast<int>(i);
  }
  return found;
}

// The stream at `Position`, clamped so that a failed FindOutputStream() lookup
// reports only its own static_assert instead of also failing to index the list.
template <int Position, typename... Streams>
using OutputStreamAt = std::tuple_element_t<(Position < 0 ? 0 : Position), std::tuple<Streams...>>;

// Builds the Any placed in KV.key. An Any key is forwarded unchanged, so a
// container can route on a key it received rather than one it constructed.
inline google::protobuf::Any BuildKey(NoKey) { return google::protobuf::Any(); }
inline google::protobuf::Any BuildKey(google::protobuf::Any key) { return key; }

template <typename KeyType>
google::protobuf::Any BuildKey(const KeyType& key) {
  google::protobuf::Any any;
  any.PackFrom(key);
  return any;
}

template <typename ValueArg>
std::string SerializePayload(ValueArg&& value) {
  if constexpr (std::is_base_of_v<google::protobuf::Message, std::decay_t<ValueArg>>) {
    return value.SerializeAsString();
  } else {
    // TODO(b/563350645): Remove the std::string branch once the legacy
    // message_counter container is deleted.
    return std::string(std::forward<ValueArg>(value));
  }
}

}  // namespace internal

// Mixin that declares the output streams a container is allowed to emit on and
// provides the strongly typed emit methods for them.
//
// Each Emit*() call is matched against the declared streams at compile time, so
// a container can only emit value types it has declared, and the destination
// key index comes from the declaration rather than the call site.
template <typename... OutputStreams>
class WithOutputs {
  static_assert(sizeof...(OutputStreams) > 0,
                "A container must declare at least one output stream.");
  // Accessing kVisibility and Value instantiates each OutputStream (checking
  // its key, value, and key index static_asserts even for streams never emitted
  // on) and verifies that no two streams share a (visibility, value type) pair.
  static_assert(((internal::FindOutputStream<OutputStreams::kVisibility,
                                             typename OutputStreams::Value, OutputStreams...>() >=
                  0) &&
                 ...),
                "Each declared output stream must have a unique (visibility, value type) pair.");

 protected:
  // Emits an encrypted value with an empty key under the declared key index.
  template <typename ValueArg>
  absl::Status EmitEncrypted(confidential_federated_compute::fns::Fn::FnContext& context,
                             ValueArg&& value) const {
    return Emit<Visibility::kEncrypted>(context, NoKey(), std::forward<ValueArg>(value));
  }

  // Emits a plaintext key packed into KV.key and an encrypted value serialized
  // into KV.data under the declared key index.
  template <typename KeyArg, typename ValueArg>
  absl::Status EmitEncrypted(confidential_federated_compute::fns::Fn::FnContext& context,
                             KeyArg&& key, ValueArg&& value) const {
    return Emit<Visibility::kEncrypted>(context, std::forward<KeyArg>(key),
                                        std::forward<ValueArg>(value));
  }

  // Emits a plaintext value with an empty key.
  template <typename ValueArg>
    requires(!std::is_same_v<std::decay_t<ValueArg>, confidential_federated_compute::Session::KV>)
  absl::Status EmitUnencrypted(confidential_federated_compute::fns::Fn::FnContext& context,
                               ValueArg&& value) const {
    return Emit<Visibility::kUnencrypted>(context, NoKey(), std::forward<ValueArg>(value));
  }

  // Emits a plaintext key packed into KV.key and a plaintext value serialized
  // into KV.data.
  template <typename KeyArg, typename ValueArg>
  absl::Status EmitUnencrypted(confidential_federated_compute::fns::Fn::FnContext& context,
                               KeyArg&& key, ValueArg&& value) const {
    return Emit<Visibility::kUnencrypted>(context, std::forward<KeyArg>(key),
                                          std::forward<ValueArg>(value));
  }

  // Forwards an already built KV pair to the orchestrator in plaintext.
  //
  // Takes its argument by rvalue reference because Session::KV has an implicit
  // converting constructor that hijacks copy construction from a non-const
  // lvalue; callers must std::move into this method.
  //
  // TODO(b/563350645): Remove once the legacy releaser container is deleted.
  absl::Status EmitUnencrypted(confidential_federated_compute::fns::Fn::FnContext& context,
                               confidential_federated_compute::Session::KV&& kv) const {
    static_assert(internal::FindOutputStream<Visibility::kUnencrypted,
                                             confidential_federated_compute::Session::KV,
                                             OutputStreams...>() >= 0,
                  "Session::KV passthrough is not declared as an Unencrypted stream in this "
                  "container's WithOutputs list.");
    if (!context.EmitUnencrypted(std::move(kv))) {
      return absl::InternalError("Failed to emit unencrypted KV to session context.");
    }
    return absl::OkStatus();
  }

 private:
  // Resolves the declared stream for (StreamVisibility, ValueArg), builds the
  // KV pair, and hands it to the session context.
  template <Visibility StreamVisibility, typename KeyArg, typename ValueArg>
  absl::Status Emit(confidential_federated_compute::fns::Fn::FnContext& context, KeyArg&& key,
                    ValueArg&& value) const {
    constexpr int kPosition =
        internal::FindOutputStream<StreamVisibility, ValueArg, OutputStreams...>();
    static_assert(kPosition >= 0,
                  "This value type is not declared as an output stream with this visibility in "
                  "the container's WithOutputs list.");

    using Stream = internal::OutputStreamAt<kPosition, OutputStreams...>;
    static_assert(std::is_same_v<typename Stream::Key, std::decay_t<KeyArg>>,
                  "The key passed here is not the key type declared for this output stream.");

    confidential_federated_compute::Session::KV kv(
        internal::BuildKey(std::forward<KeyArg>(key)),
        internal::SerializePayload(std::forward<ValueArg>(value)));

    if constexpr (StreamVisibility == Visibility::kEncrypted) {
      if (!context.EmitEncrypted(Stream::kKeyIndex, std::move(kv))) {
        return absl::InternalError("Failed to emit encrypted value to session context.");
      }
    } else {
      if (!context.EmitUnencrypted(std::move(kv))) {
        return absl::InternalError("Failed to emit unencrypted value to session context.");
      }
    }
    return absl::OkStatus();
  }
};

// Base struct providing TrustedDoFn::WithOutputs<...> for containers that
// inherit directly from confidential_federated_compute::fns::DoFn.
struct TrustedDoFn {
  // Base class combining DoFn with strongly typed output declarations.
  template <typename... OutputStreams>
  class WithOutputs : public confidential_federated_compute::fns::DoFn,
                      public ::cobalt::trusted_shuffler::WithOutputs<OutputStreams...> {
   public:
    ~WithOutputs() override = default;
  };
};

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_CONTAINERS_BASE_WITH_OUTPUTS_H_
