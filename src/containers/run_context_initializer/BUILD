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
load("//src/version:version_bzl.bzl", "version_bzl")

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "main",
    srcs = ["main.cc"],
    deps = [
        ":run_context_initializer_transform",
        "//src/version",
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
        "//src/containers/base:counter_names",
        "//src/containers/base:pipeline_nodes",
        "//src/containers/base:with_outputs",
        "//src/crypto:release_encrypter",
        "//src/crypto:secure_random",
        "//src/crypto:verify_signed_run_context",
        "//src/proto:run_context_cc_proto",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers/fns",
        "@oak//cc/containers/sdk:orchestrator_client",
        "@oak//cc/crypto:signing_key",
        "@oak//proto/crypto:crypto_cc_proto",
        "@protobuf//:any_cc_proto",
    ],
)

cc_library(
    name = "run_context_initializer_transform",
    srcs = ["run_context_initializer_transform.cc"],
    hdrs = ["run_context_initializer_transform.h"],
    deps = [
        ":run_context_initializer_fn",
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers/fns",
        "@oak//cc/containers/sdk:orchestrator_client",
        "@oak//cc/crypto:encryption_key",
        "@oak//cc/crypto:signing_key",
    ],
)

cc_test(
    name = "run_context_initializer_fn_test",
    srcs = ["run_context_initializer_fn_test.cc"],
    deps = [
        ":run_context_initializer_fn",
        "//src/containers/base:counter_names",
        "//src/crypto:release_encrypter",
        "//src/crypto:verify_signed_run_context",
        "//src/proto:run_context_cc_proto",
        "//src/testing:fake_context",
        "//src/testing:fake_keys",
        "//src/testing:test_utils",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
        "@grpc//:grpc++",
        "@oak//cc/containers/sdk:orchestrator_client",
        "@oak//cc/crypto:signing_key",
        "@oak//proto/containers:interfaces_cc_grpc",
        "@oak//proto/session:messages_cc_proto",
    ],
)

cc_test(
    name = "run_context_initializer_transform_test",
    srcs = ["run_context_initializer_transform_test.cc"],
    deps = [
        ":run_context_initializer_fn",
        ":run_context_initializer_transform",
        "//src/testing:fake_keys",
        "@abseil-cpp//absl/status:status_matchers",
        "@googletest//:gtest_main",
        "@oak//cc/containers/sdk:orchestrator_client",
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

# Generates version.bzl (GIT_VERSION) as an output artifact alongside the
# runtime bundle.
version_bzl(name = "version_bzl")
