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
        ":key_releaser_transform",
        "//src/version",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@confidential-federated-compute//containers/fns:fns",
        "@grpc//:grpc++",
        "@oak//cc/containers/sdk:encryption_key_handle",
        "@oak//cc/containers/sdk:orchestrator_client",
        "@oak//cc/containers/sdk:signing_key_handle",
        "@tink_cc//tink/aead:aead_config",
    ],
)

cc_library(
    name = "key_releaser_fn",
    srcs = ["key_releaser_fn.cc"],
    hdrs = ["key_releaser_fn.h"],
    deps = [
        "//src/containers/base:counter_names",
        "//src/containers/base:first_record_config_do_fn",
        "//src/crypto:multiset_fingerprint",
        "//src/crypto:verify_ingestion_verification_payload",
        "//src/proto:common_cc_proto",
        "//src/proto:fingerprint_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "//src/proto:release_cc_proto",
        "//src/proto:run_context_cc_proto",
        "//src/util:metric_epoch_key_utils",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@protobuf//:protobuf",
    ],
)

cc_library(
    name = "key_releaser_transform",
    srcs = ["key_releaser_transform.cc"],
    hdrs = ["key_releaser_transform.h"],
    deps = [
        ":key_releaser_fn",
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers/fns:fns",
        "@oak//cc/crypto:encryption_key",
        "@oak//cc/crypto:signing_key",
        "@protobuf//:protobuf",
    ],
)

cc_test(
    name = "key_releaser_fn_test",
    srcs = ["key_releaser_fn_test.cc"],
    deps = [
        ":key_releaser_fn",
        "//src/containers/base:counter_names",
        "//src/crypto:multiset_fingerprint",
        "//src/crypto:release_encrypter",
        "//src/crypto:verify_signed_run_context",
        "//src/proto:common_cc_proto",
        "//src/proto:fingerprint_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "//src/proto:release_cc_proto",
        "//src/proto:run_context_cc_proto",
        "//src/testing:fake_context",
        "//src/testing:test_utils",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@confidential-federated-compute//containers:session",
        "@googletest//:gtest_main",
        "@protobuf//:protobuf",
        "@tink_cc//tink:aead",
        "@tink_cc//tink:insecure_secret_key_access",
        "@tink_cc//tink:keyset_handle",
        "@tink_cc//tink:proto_keyset_format",
        "@tink_cc//tink/aead:aead_config",
        "@tink_cc//tink/config:global_registry",
        "@tink_cc//tink/util:secret_data",
    ],
)

cc_test(
    name = "key_releaser_transform_test",
    srcs = ["key_releaser_transform_test.cc"],
    deps = [
        ":key_releaser_fn",
        ":key_releaser_transform",
        "//src/crypto:multiset_fingerprint",
        "//src/crypto:release_encrypter",
        "//src/crypto:verify_signed_run_context",
        "//src/proto:common_cc_proto",
        "//src/proto:fingerprint_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "//src/proto:release_cc_proto",
        "//src/proto:run_context_cc_proto",
        "//src/testing:fake_context",
        "//src/testing:fake_keys",
        "//src/testing:test_utils",
        "//src/testing:testable_transform",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers:session",
        "@confidential-federated-compute//containers/fns:fns",
        "@federated-compute//fcp/protos/confidentialcompute:confidential_transform_cc_proto",
        "@googletest//:gtest_main",
        "@protobuf//:protobuf",
        "@tink_cc//tink/aead:aead_config",
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


