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

#include "src/containers/base/pipeline_nodes.h"

#include <string_view>

#include "gtest/gtest.h"

namespace cobalt::trusted_shuffler {
namespace {

TEST(PipelineNodesTest, NodeNameReturnsExpectedNameForKnownNodes) {
  EXPECT_EQ(NodeName(Node::kNone), "kNone");
  EXPECT_EQ(NodeName(Node::kClientUploads), "kClientUploads");
  EXPECT_EQ(NodeName(Node::kRunContext), "kRunContext");
  EXPECT_EQ(NodeName(Node::kIngestedMessages), "kIngestedMessages");
  EXPECT_EQ(NodeName(Node::kPartialIngestionReports), "kPartialIngestionReports");
  EXPECT_EQ(NodeName(Node::kVerifiedIngestion), "kVerifiedIngestion");
  EXPECT_EQ(NodeName(Node::kFilteredMessages), "kFilteredMessages");
  EXPECT_EQ(NodeName(Node::kPaddingMessages), "kPaddingMessages");
  EXPECT_EQ(NodeName(Node::kScatteredMessages), "kScatteredMessages");
  EXPECT_EQ(NodeName(Node::kGatheredMessages), "kGatheredMessages");
  EXPECT_EQ(NodeName(Node::kPartialReleaseReports), "kPartialReleaseReports");
}

TEST(PipelineNodesTest, NodeNameReturnsUnknownForUnrecognizedNode) {
  EXPECT_EQ(NodeName(static_cast<Node>(999)), "kUnknown");
  EXPECT_EQ(NodeName(static_cast<Node>(-99)), "kUnknown");
}

TEST(PipelineNodesTest, NodeNameIsConstexpr) {
  static_assert(NodeName(Node::kNone) == "kNone");
  static_assert(NodeName(Node::kClientUploads) == "kClientUploads");
  static_assert(NodeName(Node::kRunContext) == "kRunContext");
  static_assert(NodeName(static_cast<Node>(1234)) == "kUnknown");
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
