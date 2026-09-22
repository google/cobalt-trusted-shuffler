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
    name = "gather_shuffle_fn",
    srcs = ["gather_shuffle_fn.cc"],
    hdrs = ["gather_shuffle_fn.h"],
    deps = [
        "//src/batching:batch_calculator",
        "//src/containers/base:counter_names",
        "//src/containers/base:first_record_config_do_fn",
        "//src/containers/base:pipeline_nodes",
        "//src/crypto:shuffle",
        "//src/crypto:verify_ingestion_verification_payload",
        "//src/proto:common_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "//src/proto:run_context_cc_proto",
        "//src/util:metric_epoch_key_utils",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@protobuf//:protobuf",
    ],
)

cc_test(
    name = "gather_shuffle_fn_test",
    srcs = ["gather_shuffle_fn_test.cc"],
    deps = [
        ":gather_shuffle_fn",
        "//src/containers/base:counter_names",
        "//src/proto:common_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "//src/testing:fake_context",
        "//src/testing:test_utils",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
        "@protobuf//:protobuf",
    ],
)
