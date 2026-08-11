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
        ":run_context_initializer_transform",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@grpc//:grpc++",
        "@oak//cc/containers/sdk:encryption_key_handle",
        "@oak//cc/containers/sdk:orchestrator_client",
        "@oak//cc/containers/sdk:signing_key_handle",
    ],
)

cc_library(
    name = "run_context_initializer_fn",
    srcs = ["run_context_initializer_fn.cc"],
    hdrs = ["run_context_initializer_fn.h"],
    deps = [
        "//src/proto:run_context_cc_proto",
        "//src/crypto:secure_random",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers/fns:fns",
        "@oak//cc/crypto:signing_key",
    ],
)

cc_library(
    name = "run_context_initializer_transform",
    srcs = ["run_context_initializer_transform.cc"],
    hdrs = ["run_context_initializer_transform.h"],
    deps = [
        ":run_context_initializer_fn",
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers/fns:fns",
        "@oak//cc/crypto:signing_key",
        "@protobuf//:any_cc_proto",
    ],
)

cc_test(
    name = "run_context_initializer_fn_test",
    srcs = ["run_context_initializer_fn_test.cc"],
    deps = [
        ":run_context_initializer_fn",
        "//src/proto:run_context_cc_proto",
        "//src/testing:fake_context",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
        "@oak//cc/crypto:signing_key",
    ],
)

cc_test(
    name = "run_context_initializer_transform_test",
    srcs = ["run_context_initializer_transform_test.cc"],
    deps = [
        ":run_context_initializer_transform",
        "//src/testing:fake_keys",
        "@abseil-cpp//absl/status:status_matchers",
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

