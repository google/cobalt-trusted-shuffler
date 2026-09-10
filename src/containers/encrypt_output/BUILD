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
        ":encrypt_output_transform",
        "//src/version",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@confidential-federated-compute//containers/fns:fns",
        "@grpc//:grpc++",
        "@oak//cc/containers/sdk:encryption_key_handle",
        "@oak//cc/containers/sdk:orchestrator_client",
        "@oak//cc/containers/sdk:signing_key_handle",
    ],
)

cc_library(
    name = "encrypt_output_fn",
    srcs = ["encrypt_output_fn.cc"],
    hdrs = ["encrypt_output_fn.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@confidential-federated-compute//containers/fns:fns",
        "@protobuf//:any_cc_proto",
    ],
)

cc_library(
    name = "encrypt_output_transform",
    srcs = ["encrypt_output_transform.cc"],
    hdrs = ["encrypt_output_transform.h"],
    deps = [
        ":encrypt_output_fn",
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers/fns:fns",
        "@oak//cc/crypto:encryption_key",
        "@oak//cc/crypto:signing_key",
        "@protobuf//:any_cc_proto",
    ],
)

cc_test(
    name = "encrypt_output_fn_test",
    srcs = ["encrypt_output_fn_test.cc"],
    deps = [
        ":encrypt_output_fn",
        "//src/testing:fake_context",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
        "@protobuf//:any_cc_proto",
    ],
)

cc_test(
    name = "encrypt_output_transform_test",
    srcs = ["encrypt_output_transform_test.cc"],
    deps = [
        ":encrypt_output_fn",
        ":encrypt_output_transform",
        "//src/testing:fake_keys",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers/fns:fns",
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
