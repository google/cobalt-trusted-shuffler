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

#include "src/testing/integration/gather_shuffle_router.h"

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "gtest/gtest.h"
#include "src/proto/common.pb.h"
#include "src/proto/scatter_shuffle.pb.h"
#include "src/testing/integration/records.h"
#include "src/testing/test_utils.h"

namespace cobalt::trusted_shuffler::testing::integration {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::IsEmpty;
using ::testing::SizeIs;

EncryptedKVBlob CreateScatteredBlob(const MetricEpochKey& partition, int32_t group_id,
                                    const std::string& ciphertext) {
  ScatterShuffleHeader header;
  ScatterGroupKey* const group_key = header.mutable_scatter_group_key();
  *group_key->mutable_metric_epoch_key() = partition;
  group_key->set_group_id(group_id);
  google::protobuf::Any key;
  key.PackFrom(header);
  return EncryptedKVBlob(key, ciphertext);
}

TEST(GatherShuffleRouterTest, GroupsByPartitionAndGroupIdAndStripsScatterShuffleHeaderKey) {
  const EncryptedBlob verification_payload("verification_payload_bytes");
  const MetricEpochKey partition_a =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 2, .epoch_index = 10});
  const MetricEpochKey partition_b =
      CreateMetricEpochKey({.project_id = 1001, .metric_id = 3, .epoch_index = 10});

  const std::vector<EncryptedKVBlob> scattered_messages = {
      CreateScatteredBlob(partition_a, /*group_id=*/0, "a_b0_g0"),
      CreateScatteredBlob(partition_a, /*group_id=*/1, "a_b0_g1"),
      CreateScatteredBlob(partition_a, /*group_id=*/0, "a_b1_g0"),
      CreateScatteredBlob(partition_a, /*group_id=*/1, "a_b1_g1"),
      CreateScatteredBlob(partition_b, /*group_id=*/0, "b_b0_g0"),
  };

  const absl::StatusOr<std::vector<KVCollection>> groups =
      RouteToGatherShuffle(verification_payload, scattered_messages);
  ASSERT_THAT(groups, IsOk());
  ASSERT_THAT(*groups, SizeIs(3));

  // First two gather groups belong to partition_a (group_id 0 and 1), each with 2 messages.
  for (int i = 0; i < 2; ++i) {
    ASSERT_THAT((*groups)[i], SizeIs(3));
    EXPECT_EQ((*groups)[i][0].data, "verification_payload_bytes");
    EXPECT_THAT((*groups)[i][1].key.type_url(), IsEmpty());
    EXPECT_THAT((*groups)[i][2].key.type_url(), IsEmpty());
  }
  // Third gather group belongs to partition_b (group_id 0), with 1 message.
  ASSERT_THAT((*groups)[2], SizeIs(2));
  EXPECT_EQ((*groups)[2][0].data, "verification_payload_bytes");
  EXPECT_EQ((*groups)[2][1].data, "b_b0_g0");
  EXPECT_THAT((*groups)[2][1].key.type_url(), IsEmpty());
}

TEST(GatherShuffleRouterTest, RejectsMissingMetricEpochKeyInHeader) {
  const EncryptedBlob verification_payload("verification_payload_bytes");
  ScatterShuffleHeader header;
  header.mutable_scatter_group_key()->set_group_id(0);
  google::protobuf::Any key;
  key.PackFrom(header);
  const std::vector<EncryptedKVBlob> scattered_messages = {
      EncryptedKVBlob(key, "missing_partition_key"),
  };

  EXPECT_THAT(RouteToGatherShuffle(verification_payload, scattered_messages),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(GatherShuffleRouterTest, RejectsIntegrityFaultHeader) {
  const EncryptedBlob verification_payload("verification_payload_bytes");
  ScatterShuffleHeader header;
  header.mutable_integrity_fault();
  google::protobuf::Any key;
  key.PackFrom(header);
  const std::vector<EncryptedKVBlob> scattered_messages = {
      EncryptedKVBlob(key, "fault_record"),
  };

  EXPECT_THAT(RouteToGatherShuffle(verification_payload, scattered_messages),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler::testing::integration
