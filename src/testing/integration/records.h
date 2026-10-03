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

#ifndef SRC_TESTING_INTEGRATION_RECORDS_H_
#define SRC_TESTING_INTEGRATION_RECORDS_H_

#include <string>
#include <utility>
#include <vector>

#include "containers/session.h"
#include "google/protobuf/any.pb.h"
#include "src/containers/base/pipeline_nodes.h"

namespace cobalt::trusted_shuffler::testing::integration {

// A KV record routed into a container session, carrying the data node the
// payload was encrypted to alongside the key and data the session receives.
struct RoutedKV {
  // Data node the payload was encrypted to, or Node::kNone if unencrypted.
  Node node = Node::kNone;

  // Plaintext routing key passed to the container session.
  google::protobuf::Any key;

  // Serialized payload passed to the container session.
  std::string data;
};

// Sequence of routed KV records passed into a container session.
using KVCollection = std::vector<RoutedKV>;

// A container session always emits a (key, payload) pair through
// Session::Context. The host pipeline, however, declares a single element type
// per stage that determines whether the key survives past the enclave boundary:
//
//   Blob         - the key is discarded and the host sees only the payload.
//   ConfigBlobKV - the key stays in plaintext next to the payload so that the
//                  untrusted orchestrator can route on it.
//
// The four record types below are that choice crossed with whether the payload
// itself is encrypted. Using the right one at each stage boundary keeps the
// harness from routing on a key the host pipeline would never have seen.

// Payload emitted by a stage that discards the key, with ciphertext the host
// cannot read.
class EncryptedBlob {
 public:
  EncryptedBlob(Node node, std::string ciphertext)
      : node_(node), ciphertext_(std::move(ciphertext)) {}

  Node node() const { return node_; }
  const std::string& ciphertext() const { return ciphertext_; }

  // Converts the EncryptedBlob into a RoutedKV with an empty key, matching
  // what a downstream session receives when the host has no key to pass on.
  RoutedKV ToKV() const {
    return RoutedKV{.node = node_, .key = google::protobuf::Any(), .data = ciphertext_};
  }

 private:
  Node node_;
  std::string ciphertext_;
};

// Payload emitted by a stage that discards the key, with plaintext the host and
// every downstream consumer can read.
class UnencryptedBlob {
 public:
  explicit UnencryptedBlob(std::string plaintext) : plaintext_(std::move(plaintext)) {}

  const std::string& plaintext() const { return plaintext_; }

 private:
  std::string plaintext_;
};

// Routing key plus ciphertext emitted by a stage that keeps the key. The key is
// plaintext even though the payload is not.
class EncryptedKVBlob {
 public:
  EncryptedKVBlob(Node node, google::protobuf::Any key, std::string ciphertext)
      : node_(node), key_(std::move(key)), ciphertext_(std::move(ciphertext)) {}

  Node node() const { return node_; }
  const google::protobuf::Any& key() const { return key_; }
  const std::string& ciphertext() const { return ciphertext_; }

  // Drops the routing key and returns only the encrypted payload.
  EncryptedBlob DropKey() const { return EncryptedBlob(node_, ciphertext_); }

  // Returns a copy with a replaced routing key while preserving the ciphertext.
  EncryptedKVBlob WithKey(google::protobuf::Any new_key) const {
    return EncryptedKVBlob(node_, std::move(new_key), ciphertext_);
  }

  // Converts the EncryptedKVBlob into a RoutedKV.
  RoutedKV ToKV() const { return RoutedKV{.node = node_, .key = key_, .data = ciphertext_}; }

 private:
  Node node_;
  google::protobuf::Any key_;
  std::string ciphertext_;
};

// Routing key plus plaintext emitted by a stage that keeps the key.
class UnencryptedKVBlob {
 public:
  UnencryptedKVBlob(google::protobuf::Any key, std::string plaintext)
      : key_(std::move(key)), plaintext_(std::move(plaintext)) {}

  const google::protobuf::Any& key() const { return key_; }
  const std::string& plaintext() const { return plaintext_; }

  // Drops the routing key and returns only the unencrypted payload.
  UnencryptedBlob DropKey() const { return UnencryptedBlob(plaintext_); }

  // Returns a copy with a replaced routing key while preserving the plaintext.
  UnencryptedKVBlob WithKey(google::protobuf::Any new_key) const {
    return UnencryptedKVBlob(std::move(new_key), plaintext_);
  }

 private:
  google::protobuf::Any key_;
  std::string plaintext_;
};

// Host-visible output of a stage that discards keys. Encrypted and unencrypted
// records travel in one host collection and are told apart by their metadata,
// so a stage cannot keep keys on some of its records and drop them on others.
struct BlobStageOutputs {
  // Encrypted blobs emitted by the stage.
  std::vector<EncryptedBlob> encrypted;

  // Unencrypted blobs emitted by the stage.
  std::vector<UnencryptedBlob> unencrypted;

  // Accumulates the outputs of another session of the same stage, used when a
  // stage runs multiple sessions.
  void Append(BlobStageOutputs&& other) {
    for (EncryptedBlob& record : other.encrypted) {
      encrypted.push_back(std::move(record));
    }
    for (UnencryptedBlob& record : other.unencrypted) {
      unencrypted.push_back(std::move(record));
    }
  }
};

// Host-visible output of a stage that keeps keys.
struct KVBlobStageOutputs {
  // Encrypted KV blobs emitted by the stage.
  std::vector<EncryptedKVBlob> encrypted;

  // Unencrypted KV blobs emitted by the stage.
  std::vector<UnencryptedKVBlob> unencrypted;

  // Accumulates the outputs of another session of the same stage, used when a
  // stage runs multiple sessions.
  void Append(KVBlobStageOutputs&& other) {
    for (EncryptedKVBlob& record : other.encrypted) {
      encrypted.push_back(std::move(record));
    }
    for (UnencryptedKVBlob& record : other.unencrypted) {
      unencrypted.push_back(std::move(record));
    }
  }
};

}  // namespace cobalt::trusted_shuffler::testing::integration

#endif  // SRC_TESTING_INTEGRATION_RECORDS_H_
