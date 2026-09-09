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

load("@oak//bazel:defs.bzl", "oci_runtime_bundle")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_oci//oci:defs.bzl", "oci_image")
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")

cc_binary(
    name = "main",
    srcs = ["main.cc"],
    deps = [
        ":filter_and_strip_privacy_id_transform",
        "//src/version",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@grpc//:grpc++",
        "@oak//cc/containers/sdk:encryption_key_handle",
        "@oak//cc/containers/sdk:orchestrator_client",
        "@oak//cc/containers/sdk:signing_key_handle",
        "@oak//proto/containers:orchestrator_crypto_cc_grpc",
    ],
)

cc_library(
    name = "filter_and_strip_privacy_id_fn",
    srcs = ["filter_and_strip_privacy_id_fn.cc"],
    hdrs = ["filter_and_strip_privacy_id_fn.h"],
    deps = [
        "//src/proto:config_constraints_cc_proto",
        "@abseil-cpp//absl/status",
        "@confidential-federated-compute//containers/fns:fns",
    ],
)

cc_library(
    name = "filter_and_strip_privacy_id_transform",
    srcs = ["filter_and_strip_privacy_id_transform.cc"],
    hdrs = ["filter_and_strip_privacy_id_transform.h"],
    deps = [
        ":filter_and_strip_privacy_id_fn",
        "//src/proto:config_constraints_cc_proto",
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers/fns:fns",
        "@protobuf//:any_cc_proto",
    ],
)

cc_test(
    name = "filter_and_strip_privacy_id_fn_test",
    srcs = ["filter_and_strip_privacy_id_fn_test.cc"],
    deps = [
        ":filter_and_strip_privacy_id_fn",
        "//src/proto:config_constraints_cc_proto",
        "//src/testing:fake_context",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "filter_and_strip_privacy_id_transform_test",
    srcs = ["filter_and_strip_privacy_id_transform_test.cc"],
    deps = [
        ":filter_and_strip_privacy_id_transform",
        "//src/testing:fake_context",
        "//src/testing:fake_keys",
        "//src/testing:testable_transform",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@googletest//:gtest_main",
    ],
)

pkg_tar(
    name = "tar",
    srcs = [":main"],
)

oci_image(
    name = "oci_image",
    base = "@distroless_cc_debian12_linux_amd64",
    entrypoint = ["/main"],
    tars = [":tar"],
)

oci_runtime_bundle(
    name = "oci_runtime_bundle",
    image = ":oci_image",
)
