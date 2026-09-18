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
        ":ingestion_verification_transform",
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
    name = "ingestion_verification_fn",
    srcs = ["ingestion_verification_fn.cc"],
    hdrs = ["ingestion_verification_fn.h"],
    deps = [
        "//src/containers/base:first_record_config_do_fn",
        "//src/crypto:multiset_fingerprint",
        "//src/crypto:verify_signed_run_context",
        "//src/proto:common_cc_proto",
        "//src/proto:config_constraints_cc_proto",
        "//src/proto:fingerprint_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "//src/proto:run_context_cc_proto",
        "//src/sketch:hll_sketch",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers/fns:fns",
        "@protobuf//:any_cc_proto",
    ],
)

cc_library(
    name = "ingestion_verification_transform",
    srcs = ["ingestion_verification_transform.cc"],
    hdrs = ["ingestion_verification_transform.h"],
    deps = [
        ":ingestion_verification_fn",
        "//src/proto:config_constraints_cc_proto",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@confidential-federated-compute//containers/fns:fns",
        "@oak//cc/crypto:encryption_key",
        "@oak//cc/crypto:signing_key",
        "@protobuf//:any_cc_proto",
    ],
)

cc_test(
    name = "ingestion_verification_fn_test",
    srcs = ["ingestion_verification_fn_test.cc"],
    deps = [
        ":ingestion_verification_fn",
        "//src/crypto:multiset_fingerprint",
        "//src/crypto:release_encrypter",
        "//src/crypto:verify_signed_run_context",
        "//src/proto:common_cc_proto",
        "//src/proto:config_constraints_cc_proto",
        "//src/proto:fingerprint_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "//src/proto:run_context_cc_proto",
        "//src/proto:sketch_cc_proto",
        "//src/sketch:hll_sketch",
        "//src/testing:fake_context",
        "//src/testing:matchers",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
        "@protobuf//:any_cc_proto",
    ],
)

cc_test(
    name = "ingestion_verification_transform_test",
    srcs = ["ingestion_verification_transform_test.cc"],
    deps = [
        ":ingestion_verification_fn",
        ":ingestion_verification_transform",
        "//src/crypto:multiset_fingerprint",
        "//src/crypto:release_encrypter",
        "//src/crypto:verify_signed_run_context",
        "//src/proto:common_cc_proto",
        "//src/proto:config_constraints_cc_proto",
        "//src/proto:fingerprint_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "//src/proto:run_context_cc_proto",
        "//src/sketch:hll_sketch",
        "//src/testing:fake_context",
        "//src/testing:fake_keys",
        "//src/testing:matchers",
        "@abseil-cpp//absl/log:absl_check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@federated-compute//fcp/protos/confidentialcompute:confidential_transform_cc_proto",
        "@googletest//:gtest_main",
        "@protobuf//:any_cc_proto",
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
