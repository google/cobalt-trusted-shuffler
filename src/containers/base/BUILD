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

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "first_record_config_do_fn",
    hdrs = ["first_record_config_do_fn.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@confidential-federated-compute//containers/fns:fns",
        "@protobuf//:protobuf",
    ],
)

cc_test(
    name = "first_record_config_do_fn_test",
    srcs = ["first_record_config_do_fn_test.cc"],
    deps = [
        ":first_record_config_do_fn",
        "//src/proto:run_context_cc_proto",
        "//src/testing:fake_context",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
    ],
)
