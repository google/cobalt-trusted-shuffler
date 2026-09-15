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
        ":ingest_uploads_transform",
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
    name = "upload_parser",
    srcs = ["upload_parser.cc"],
    hdrs = ["upload_parser.h"],
    deps = [
        "//src/proto:trusted_shuffler_upload_cc_proto",
        "//src/util:time_utils",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@federated-compute//fcp/confidentialcompute:client_payload",
        "@org_tensorflow_federated//tensorflow_federated/cc/core/impl/aggregation/protocol:federated_compute_checkpoint_parser",
    ],
)

cc_test(
    name = "upload_parser_test",
    srcs = ["upload_parser_test.cc"],
    deps = [
        ":upload_parser",
        "//src/proto:trusted_shuffler_upload_cc_proto",
        "//src/testing:matchers",
        "//src/testing:test_utils",
        "//src/util:time_utils",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@federated-compute//fcp/confidentialcompute:client_payload",
        "@googletest//:gtest_main",
        "@org_tensorflow_federated//tensorflow_federated/cc/core/impl/aggregation/core:tensor",
        "@org_tensorflow_federated//tensorflow_federated/cc/core/impl/aggregation/protocol:federated_compute_checkpoint_builder",
    ],
)

cc_library(
    name = "ingest_uploads_fn",
    srcs = ["ingest_uploads_fn.cc"],
    hdrs = ["ingest_uploads_fn.h"],
    deps = [
        ":upload_parser",
        "//src/containers/base:first_record_config_do_fn",
        "//src/crypto:multiset_fingerprint",
        "//src/crypto:verify_signed_run_context",
        "//src/proto:common_cc_proto",
        "//src/proto:fingerprint_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "//src/proto:run_context_cc_proto",
        "//src/proto:trusted_shuffler_upload_cc_proto",
        "//src/sketch:hll_sketch",
        "//src/util:time_utils",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@confidential-federated-compute//containers/fns:fns",
        "@federated-compute//fcp/protos/confidentialcompute:blob_data_cc_proto",
        "@protobuf//:any_cc_proto",
    ],
)

cc_library(
    name = "ingest_uploads_transform",
    srcs = ["ingest_uploads_transform.cc"],
    hdrs = ["ingest_uploads_transform.h"],
    deps = [
        ":ingest_uploads_fn",
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers/fns:fns",
        "@protobuf//:any_cc_proto",
    ],
)

cc_test(
    name = "ingest_uploads_fn_test",
    srcs = ["ingest_uploads_fn_test.cc"],
    deps = [
        ":ingest_uploads_fn",
        "//src/crypto:verify_signed_run_context",
        "//src/proto:common_cc_proto",
        "//src/proto:fingerprint_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "//src/proto:run_context_cc_proto",
        "//src/proto:trusted_shuffler_upload_cc_proto",
        "//src/sketch:hll_sketch",
        "//src/testing:fake_context",
        "//src/testing:test_utils",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@federated-compute//fcp/confidentialcompute:client_payload",
        "@federated-compute//fcp/protos/confidentialcompute:blob_data_cc_proto",
        "@googletest//:gtest_main",
        "@org_tensorflow_federated//tensorflow_federated/cc/core/impl/aggregation/core:tensor",
        "@org_tensorflow_federated//tensorflow_federated/cc/core/impl/aggregation/protocol:federated_compute_checkpoint_builder",
    ],
)

cc_test(
    name = "ingest_uploads_transform_test",
    srcs = ["ingest_uploads_transform_test.cc"],
    deps = [
        ":ingest_uploads_transform",
        "//src/testing:fake_context",
        "//src/testing:fake_keys",
        "//src/testing:testable_transform",
        "@abseil-cpp//absl/status:status",
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
