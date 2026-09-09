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
    name = "secure_random",
    srcs = ["secure_random.cc"],
    hdrs = ["secure_random.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@boringssl//:crypto",
    ],
)

cc_test(
    name = "secure_random_test",
    srcs = ["secure_random_test.cc"],
    deps = [
        ":secure_random",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "multiset_fingerprint",
    srcs = ["multiset_fingerprint.cc"],
    hdrs = ["multiset_fingerprint.h"],
    deps = [
        "//src/proto:common_cc_proto",
        "//src/proto:fingerprint_cc_proto",
        "//src/proto:run_context_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@boringssl//:crypto",
    ],
)

cc_test(
    name = "multiset_fingerprint_test",
    srcs = ["multiset_fingerprint_test.cc"],
    data = [
        "testdata/multiset_fingerprint_golden_test_vectors.json",
    ],
    deps = [
        ":multiset_fingerprint",
        "//src/testing:multiset_fingerprint_golden_test_vectors_cc_proto",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
        "@protobuf//:protobuf",
    ],
)

cc_library(
    name = "shuffle",
    srcs = ["shuffle.cc"],
    hdrs = ["shuffle.h"],
    deps = [
        "@abseil-cpp//absl/functional:function_ref",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@boringssl//:crypto",
    ],
)

cc_test(
    name = "shuffle_test",
    srcs = ["shuffle_test.cc"],
    deps = [
        ":shuffle",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "verify_signed_run_context",
    srcs = ["verify_signed_run_context.cc"],
    hdrs = ["verify_signed_run_context.h"],
    deps = [
        "//src/proto:run_context_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

alias(
    name = "run_context_verifier",
    actual = ":verify_signed_run_context",
)

cc_test(
    name = "verify_signed_run_context_test",
    srcs = ["verify_signed_run_context_test.cc"],
    deps = [
        ":verify_signed_run_context",
        "//src/crypto:secure_random",
        "//src/proto:run_context_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
    ],
)

