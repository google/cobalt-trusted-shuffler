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
    name = "pre_padding_fn",
    srcs = ["pre_padding_fn.cc"],
    hdrs = ["pre_padding_fn.h"],
    deps = [
        "//src/batching:batch_calculator",
        "//src/containers/base:counter_names",
        "//src/containers/base:first_record_config_do_fn",
        "//src/containers/base:pipeline_nodes",
        "//src/containers/base:with_outputs",
        "//src/crypto:verify_ingestion_verification_payload",
        "//src/proto:common_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "@abseil-cpp//absl/status",
        "@confidential-federated-compute//containers/fns:fns",
        "@protobuf//:protobuf",
    ],
)

cc_test(
    name = "pre_padding_fn_test",
    srcs = ["pre_padding_fn_test.cc"],
    deps = [
        ":pre_padding_fn",
        "//src/containers/base:counter_names",
        "//src/crypto:release_encrypter",
        "//src/crypto:verify_signed_run_context",
        "//src/proto:common_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "//src/proto:run_context_cc_proto",
        "//src/testing:fake_context",
        "//src/testing:matchers",
        "//src/testing:test_utils",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
    ],
)
