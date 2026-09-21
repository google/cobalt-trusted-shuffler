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

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "main",
    srcs = ["main.cc"],
    deps = [
        ":scatter_shuffle_transform",
        "//src/version",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@confidential-federated-compute//containers/fns:fns",
        "@grpc//:grpc++",
        "@oak//cc/containers/sdk:encryption_key_handle",
        "@oak//cc/containers/sdk:orchestrator_client",
        "@oak//cc/containers/sdk:signing_key_handle",
        "@oak//proto/containers:orchestrator_crypto_cc_grpc",
    ],
)

cc_library(
    name = "scatter_shuffle_fn",
    srcs = ["scatter_shuffle_fn.cc"],
    hdrs = ["scatter_shuffle_fn.h"],
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
        "//src/proto:scatter_shuffle_cc_proto",
        "//src/util:metric_epoch_key_utils",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@confidential-federated-compute//containers/fns:fns",
        "@protobuf//:any_cc_proto",
    ],
)

cc_library(
    name = "scatter_shuffle_transform",
    srcs = ["scatter_shuffle_transform.cc"],
    hdrs = ["scatter_shuffle_transform.h"],
    deps = [
        ":scatter_shuffle_fn",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers/fns:fns",
        "@oak//cc/crypto:encryption_key",
        "@oak//cc/crypto:signing_key",
        "@protobuf//:any_cc_proto",
    ],
)

cc_test(
    name = "scatter_shuffle_fn_test",
    size = "small",
    srcs = ["scatter_shuffle_fn_test.cc"],
    deps = [
        ":scatter_shuffle_fn",
        "//src/containers/base:counter_names",
        "//src/crypto:release_encrypter",
        "//src/proto:common_cc_proto",
        "//src/proto:fingerprint_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "//src/proto:run_context_cc_proto",
        "//src/proto:scatter_shuffle_cc_proto",
        "//src/testing:fake_context",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@googletest//:gtest_main",
        "@protobuf//:any_cc_proto",
    ],
)

cc_test(
    name = "scatter_shuffle_transform_test",
    size = "small",
    srcs = ["scatter_shuffle_transform_test.cc"],
    deps = [
        ":scatter_shuffle_fn",
        ":scatter_shuffle_transform",
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
