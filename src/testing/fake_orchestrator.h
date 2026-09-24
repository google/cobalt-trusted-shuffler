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

#ifndef SRC_TESTING_FAKE_ORCHESTRATOR_H_
#define SRC_TESTING_FAKE_ORCHESTRATOR_H_

#include <memory>
#include <utility>

#include "cc/containers/sdk/orchestrator_client.h"
#include "google/protobuf/empty.pb.h"
#include "grpcpp/channel.h"
#include "grpcpp/create_channel.h"
#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "proto/containers/interfaces.grpc.pb.h"
#include "proto/session/messages.pb.h"

namespace cobalt::trusted_shuffler::testing {

class FakeOrchestratorService final : public oak::containers::Orchestrator::Service {
 public:
  FakeOrchestratorService() : status_(grpc::Status::OK) {}
  explicit FakeOrchestratorService(oak::session::v1::EndorsedEvidence evidence)
      : evidence_(std::move(evidence)), status_(grpc::Status::OK) {}

  grpc::Status GetEndorsedEvidence(grpc::ServerContext* context,
                                   const google::protobuf::Empty* request,
                                   oak::session::v1::EndorsedEvidence* response) override {
    if (!status_.ok()) {
      return status_;
    }
    *response = evidence_;
    return grpc::Status::OK;
  }

  void SetEvidence(oak::session::v1::EndorsedEvidence evidence) {
    evidence_ = std::move(evidence);
    status_ = grpc::Status::OK;
  }

  void SetStatus(grpc::Status status) { status_ = std::move(status); }

 private:
  oak::session::v1::EndorsedEvidence evidence_;
  grpc::Status status_;
};

class FakeOrchestratorServer {
 public:
  explicit FakeOrchestratorServer(oak::session::v1::EndorsedEvidence evidence)
      : service_(std::move(evidence)) {
    grpc::ServerBuilder builder;
    builder.RegisterService(&service_);
    server_ = builder.BuildAndStart();
    client_ = std::make_unique<oak::containers::sdk::OrchestratorClient>(
        server_->InProcessChannel(grpc::ChannelArguments()));
  }

  FakeOrchestratorService& Service() { return service_; }
  const oak::containers::sdk::OrchestratorClient* Client() const { return client_.get(); }

 private:
  FakeOrchestratorService service_;
  std::unique_ptr<grpc::Server> server_;
  std::unique_ptr<oak::containers::sdk::OrchestratorClient> client_;
};

}  // namespace cobalt::trusted_shuffler::testing

#endif  // SRC_TESTING_FAKE_ORCHESTRATOR_H_
