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
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"

namespace cobalt::trusted_shuffler {

absl::StatusOr<Epoch> Epoch::FromEventTime(const absl::string_view event_time_str) {
  const absl::StatusOr<absl::Time> parsed_time = ParseTime(event_time_str);
  if (!parsed_time.ok()) {
    return parsed_time.status();
  }
  return FromTime(*parsed_time);
}

Epoch Epoch::FromTime(const absl::Time time) {
  const absl::CivilDay civil_day = absl::ToCivilDay(time, absl::UTCTimeZone());
  return Epoch(civil_day - absl::CivilDay(1970, 1, 1));
}

absl::StatusOr<absl::Time> ParseTime(const absl::string_view time_str) {
  if (time_str.empty()) {
    return absl::InvalidArgumentError("Time string is empty");
  }

  absl::Time time;
  std::string err;
  if (absl::ParseTime(absl::RFC3339_full, time_str, absl::UTCTimeZone(), &time, &err) ||
      absl::ParseTime(absl::RFC3339_sec, time_str, absl::UTCTimeZone(), &time, &err)) {
    return time;
  }

  absl::CivilDay civil_day;
  if (absl::ParseCivilTime(time_str, &civil_day)) {
    return absl::FromCivil(civil_day, absl::UTCTimeZone());
  }

  return absl::InvalidArgumentError(absl::StrCat("Failed to parse time string: ", time_str));
}

}  // namespace cobalt::trusted_shuffler
