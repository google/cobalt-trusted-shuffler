# Copyright 2026 Google LLC.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

genrule(
    name = "version_header",
    outs = ["version.h"],
    cmd = """
GIT_COMMIT="unknown"
BUILD_ID="unknown"
if [ -f bazel-out/stable-status.txt ]; then
  GIT_COMMIT=$$(grep '^STABLE_GIT_COMMIT ' bazel-out/stable-status.txt 2>/dev/null | cut -d ' ' -f 2)
  BUILD_ID=$$(grep '^STABLE_BUILD_ID ' bazel-out/stable-status.txt 2>/dev/null | cut -d ' ' -f 2)
fi
cat << 'HEADER' > $@
#ifndef SRC_VERSION_VERSION_H_
#define SRC_VERSION_VERSION_H_

#include "absl/log/log.h"
#include "absl/strings/string_view.h"

namespace cobalt::trusted_shuffler {

inline constexpr absl::string_view kGitCommit = "__GIT_COMMIT__";
inline constexpr absl::string_view kBuildId = "__BUILD_ID__";

// Logs container startup details at INFO level, including container name,
// GoB commit, and build ID.
inline void LogContainerStartup(absl::string_view container_name) {
  LOG(INFO) << container_name << " container starting up (GoB commit: "
            << kGitCommit << ", build: " << kBuildId << ")";
}

}  // namespace cobalt::trusted_shuffler

#endif  // SRC_VERSION_VERSION_H_
HEADER
sed -i "s/__GIT_COMMIT__/$${GIT_COMMIT:-unknown}/" $@
sed -i "s/__BUILD_ID__/$${BUILD_ID:-unknown}/" $@
""",
    stamp = 1,
)

cc_library(
    name = "version",
    hdrs = [":version_header"],
    visibility = ["//src:__subpackages__"],
    deps = [
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "version_test",
    srcs = ["version_test.cc"],
    deps = [
        ":version",
        "@googletest//:gtest_main",
    ],
)
