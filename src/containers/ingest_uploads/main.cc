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
#include "src/containers/ingest_uploads/ingest_uploads_transform.h"
#include "src/version/version.h"

using grpc::Server;
using grpc::ServerBuilder;
using oak::containers::sdk::InstanceEncryptionKeyHandle;
using oak::containers::sdk::InstanceSigningKeyHandle;
using oak::containers::sdk::OrchestratorClient;

namespace cobalt::trusted_shuffler {
namespace {

void RunServer() {
  LogContainerStartup("Ingest Uploads");
  std::string server_address("[::]:8080");

  std::unique_ptr<confidential_federated_compute::fns::FnConfidentialTransform> service =
      CreateIngestUploadsTransform(std::make_unique<InstanceSigningKeyHandle>(),
                                   std::make_unique<InstanceEncryptionKeyHandle>());
  ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(service.get());
  std::unique_ptr<Server> server = builder.BuildAndStart();
  LOG(INFO) << "Ingest Uploads Transform Server listening on " << server_address;

  CHECK_OK(OrchestratorClient().NotifyAppReady());
  server->Wait();
}

}  // namespace
}  // namespace cobalt::trusted_shuffler

int main(int argc, char** argv) {
  cobalt::trusted_shuffler::RunServer();
  return EXIT_SUCCESS;
}
