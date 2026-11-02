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

#include <cstdlib>
#include <memory>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "cc/containers/sdk/encryption_key_handle.h"
#include "cc/containers/sdk/orchestrator_client.h"
#include "cc/containers/sdk/signing_key_handle.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "src/containers/key_releaser/key_releaser_transform.h"
#include "tink/aead/aead_config.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::confidential_federated_compute::fns::FnConfidentialTransform;
using ::grpc::Server;
using ::grpc::ServerBuilder;
using ::oak::containers::sdk::InstanceEncryptionKeyHandle;
using ::oak::containers::sdk::InstanceSigningKeyHandle;
using ::oak::containers::sdk::OrchestratorClient;

constexpr char kServerAddress[] = "[::]:8080";

void RunServer() {
  CHECK_OK(crypto::tink::AeadConfig::Register());

  const std::unique_ptr<FnConfidentialTransform> service =
      CreateKeyReleaserTransform(std::make_unique<InstanceSigningKeyHandle>(),
                                 std::make_unique<InstanceEncryptionKeyHandle>());
  ServerBuilder builder;
  builder.AddListeningPort(kServerAddress, grpc::InsecureServerCredentials());
  builder.RegisterService(service.get());
  const std::unique_ptr<Server> server = builder.BuildAndStart();
  CHECK_NE(server, nullptr);
  LOG(INFO) << "Key Releaser Transform Server listening on " << kServerAddress;

  CHECK_OK(OrchestratorClient().NotifyAppReady());
  server->Wait();
}

}  // namespace
}  // namespace cobalt::trusted_shuffler

int main(int /*argc*/, char** /*argv*/) {
  cobalt::trusted_shuffler::RunServer();
  return EXIT_SUCCESS;
}
