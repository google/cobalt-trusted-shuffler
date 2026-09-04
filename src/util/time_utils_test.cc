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

#include "src/util/time_utils.h"

#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace cobalt::trusted_shuffler {
namespace {

using absl_testing::IsOk;
using absl_testing::StatusIs;
using ::testing::Eq;

absl::StatusOr<int64_t> EventTimeToEpochDays(absl::string_view event_time_str) {
  const absl::StatusOr<Epoch> epoch = Epoch::FromEventTime(event_time_str);
  if (!epoch.ok()) {
    return epoch.status();
  }
  return epoch->Days();
}

int64_t TimeToEpochDays(absl::Time time) { return Epoch::FromTime(time).Days(); }

TEST(TimeUtilsTest, ConvertsRfc3339TimestampToEpochDays) {
  const absl::StatusOr<int64_t> epoch_start = EventTimeToEpochDays("1970-01-01T00:00:00Z");
  ASSERT_THAT(epoch_start, IsOk());
  EXPECT_THAT(*epoch_start, Eq(0));

  const absl::StatusOr<int64_t> epoch_end_of_day = EventTimeToEpochDays("1970-01-01T23:59:59Z");
  ASSERT_THAT(epoch_end_of_day, IsOk());
  EXPECT_THAT(*epoch_end_of_day, Eq(0));

  const absl::StatusOr<int64_t> epoch_fractional =
      EventTimeToEpochDays("1970-01-01T23:59:59.999999Z");
  ASSERT_THAT(epoch_fractional, IsOk());
  EXPECT_THAT(*epoch_fractional, Eq(0));

  const absl::StatusOr<int64_t> epoch_next_day = EventTimeToEpochDays("1970-01-02T00:00:00Z");
  ASSERT_THAT(epoch_next_day, IsOk());
  EXPECT_THAT(*epoch_next_day, Eq(1));

  const absl::StatusOr<int64_t> epoch_2026 = EventTimeToEpochDays("2026-09-03T18:00:00Z");
  ASSERT_THAT(epoch_2026, IsOk());
  EXPECT_THAT(*epoch_2026, Eq(20699));

  const absl::StatusOr<int64_t> epoch_leap_day = EventTimeToEpochDays("2024-02-29T12:00:00Z");
  ASSERT_THAT(epoch_leap_day, IsOk());
  EXPECT_THAT(*epoch_leap_day, Eq(19782));

  const absl::StatusOr<int64_t> epoch_pre_1970 = EventTimeToEpochDays("1969-12-31T23:59:59Z");
  ASSERT_THAT(epoch_pre_1970, IsOk());
  EXPECT_THAT(*epoch_pre_1970, Eq(-1));
}

TEST(TimeUtilsTest, ConvertsCivilDateToEpochDays) {
  const absl::StatusOr<int64_t> epoch_1970 = EventTimeToEpochDays("1970-01-01");
  ASSERT_THAT(epoch_1970, IsOk());
  EXPECT_THAT(*epoch_1970, Eq(0));

  const absl::StatusOr<int64_t> epoch_1970_next = EventTimeToEpochDays("1970-01-02");
  ASSERT_THAT(epoch_1970_next, IsOk());
  EXPECT_THAT(*epoch_1970_next, Eq(1));

  const absl::StatusOr<int64_t> epoch_2026 = EventTimeToEpochDays("2026-09-03");
  ASSERT_THAT(epoch_2026, IsOk());
  EXPECT_THAT(*epoch_2026, Eq(20699));

  const absl::StatusOr<int64_t> epoch_pre_1970 = EventTimeToEpochDays("1969-12-31");
  ASSERT_THAT(epoch_pre_1970, IsOk());
  EXPECT_THAT(*epoch_pre_1970, Eq(-1));
}

TEST(TimeUtilsTest, ConvertsTimestampWithOffsetToUtcEpochDays) {
  // 1970-01-01T02:00:00+03:00 is 1969-12-31T23:00:00Z (pre-epoch day -1).
  const absl::StatusOr<int64_t> epoch_pre = EventTimeToEpochDays("1970-01-01T02:00:00+03:00");
  ASSERT_THAT(epoch_pre, IsOk());
  EXPECT_THAT(*epoch_pre, Eq(-1));

  // 1970-01-01T23:00:00-05:00 is 1970-01-02T04:00:00Z (epoch day 1).
  const absl::StatusOr<int64_t> epoch_post = EventTimeToEpochDays("1970-01-01T23:00:00-05:00");
  ASSERT_THAT(epoch_post, IsOk());
  EXPECT_THAT(*epoch_post, Eq(1));
}

TEST(TimeUtilsTest, ConvertsTimeObjectToEpochDays) {
  EXPECT_THAT(TimeToEpochDays(absl::UnixEpoch()), Eq(0));
  EXPECT_THAT(TimeToEpochDays(absl::UnixEpoch() + absl::Hours(23)), Eq(0));
  EXPECT_THAT(TimeToEpochDays(absl::UnixEpoch() + absl::Hours(24)), Eq(1));
  EXPECT_THAT(TimeToEpochDays(absl::UnixEpoch() - absl::Seconds(1)), Eq(-1));
}

TEST(TimeUtilsTest, ReturnsInvalidArgumentOnInvalidEventTime) {
  EXPECT_THAT(EventTimeToEpochDays(""), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(EventTimeToEpochDays("invalid-time-format"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(EventTimeToEpochDays("2026-09"), StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(EventTimeToEpochDays("20260903T180000Z"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(EventTimeToEpochDays("2026-13-01T00:00:00Z"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(EventTimeToEpochDays("2026-02-30T00:00:00Z"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(TimeUtilsTest, EpochFromEventTimeAndComparisons) {
  const absl::StatusOr<Epoch> epoch_1970 = Epoch::FromEventTime("1970-01-01T00:00:00Z");
  ASSERT_THAT(epoch_1970, IsOk());
  EXPECT_THAT(*epoch_1970, Eq(Epoch::FromDays(0)));
  EXPECT_THAT(epoch_1970->Days(), Eq(0));

  const absl::StatusOr<Epoch> epoch_2026 = Epoch::FromEventTime("2026-09-03T18:00:00Z");
  ASSERT_THAT(epoch_2026, IsOk());
  EXPECT_THAT(*epoch_2026, Eq(Epoch::FromDays(20699)));

  EXPECT_TRUE(*epoch_1970 < *epoch_2026);
  EXPECT_TRUE(*epoch_2026 > *epoch_1970);
  EXPECT_TRUE(*epoch_1970 <= Epoch::FromDays(0));
  EXPECT_TRUE(*epoch_1970 >= Epoch::FromDays(0));

  EXPECT_THAT(absl::StrCat(*epoch_2026), Eq("Epoch(20699)"));

  EXPECT_THAT(Epoch::FromEventTime("invalid"), StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(TimeUtilsTest, EpochFromTime) {
  EXPECT_THAT(Epoch::FromTime(absl::UnixEpoch()), Eq(Epoch::FromDays(0)));
  EXPECT_THAT(Epoch::FromTime(absl::UnixEpoch() + absl::Hours(24)), Eq(Epoch::FromDays(1)));
  EXPECT_THAT(Epoch::FromTime(absl::UnixEpoch() - absl::Seconds(1)), Eq(Epoch::FromDays(-1)));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
