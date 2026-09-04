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

#ifndef SRC_UTIL_TIME_UTILS_H_
#define SRC_UTIL_TIME_UTILS_H_

#include <cstdint>
#include <ostream>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"

namespace cobalt::trusted_shuffler {

// Strong value type representing an epoch measured in Unix Epoch Days
// (number of days since Jan 1st 1970 UTC).
class Epoch {
 public:
  // Converts an RFC3339 timestamp or civil date string ("YYYY-MM-DD") into an Epoch.
  static absl::StatusOr<Epoch> FromEventTime(absl::string_view event_time_str);

  // Converts an absl::Time object into an Epoch.
  static Epoch FromTime(absl::Time time);

  // Constructs an Epoch directly from a day count.
  static constexpr Epoch FromDays(const int64_t days) { return Epoch(days); }

  // Default constructor (epoch day 0, i.e. 1970-01-01 UTC).
  constexpr Epoch() : days_(0) {}

  // Returns the raw epoch day index.
  constexpr int64_t Days() const { return days_; }

  constexpr bool operator==(const Epoch& other) const = default;
  constexpr auto operator<=>(const Epoch& other) const = default;

  template <typename H>
  friend H AbslHashValue(H h, const Epoch& epoch) {
    return H::combine(std::move(h), epoch.days_);
  }

  template <typename Sink>
  friend void AbslStringify(Sink& sink, const Epoch& epoch) {
    absl::Format(&sink, "Epoch(%d)", epoch.days_);
  }

  friend std::ostream& operator<<(std::ostream& os, const Epoch& epoch) {
    return os << "Epoch(" << epoch.days_ << ")";
  }

 private:
  constexpr explicit Epoch(const int64_t days) : days_(days) {}

  int64_t days_;
};

// Parses an RFC3339 timestamp or civil date string ("YYYY-MM-DD") into absl::Time.
absl::StatusOr<absl::Time> ParseTime(absl::string_view time_str);

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_UTIL_TIME_UTILS_H_
