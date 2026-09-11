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
    name = "batch_calculator",
    srcs = ["batch_calculator.cc"],
    hdrs = ["batch_calculator.h"],
    deps = [
        "//src/crypto:verify_signed_run_context",
        "//src/proto:common_cc_proto",
        "//src/proto:fingerprint_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "//src/proto:run_context_cc_proto",
        "//src/util:metric_epoch_key_utils",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "batch_calculator_test",
    size = "small",
    srcs = ["batch_calculator_test.cc"],
    deps = [
        ":batch_calculator",
        "//src/crypto:release_encrypter",
        "//src/proto:common_cc_proto",
        "//src/proto:fingerprint_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "//src/proto:run_context_cc_proto",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
    ],
)
