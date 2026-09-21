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
    name = "records",
    hdrs = ["records.h"],
    deps = [
        "@confidential-federated-compute//containers:session",
        "@protobuf//:protobuf",
    ],
)

cc_library(
    name = "simulated_context",
    srcs = ["simulated_context.cc"],
    hdrs = ["simulated_context.h"],
    deps = [
        ":records",
        "@abseil-cpp//absl/log:check",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings:string_view",
        "@confidential-federated-compute//containers:session",
        "@federated-compute//fcp/protos/confidentialcompute:confidential_transform_cc_proto",
    ],
)

cc_library(
    name = "do_fn_runner",
    srcs = ["do_fn_runner.cc"],
    hdrs = ["do_fn_runner.h"],
    deps = [
        ":simulated_context",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
        "@confidential-federated-compute//containers:session",
        "@confidential-federated-compute//containers/fns",
        "@protobuf//:protobuf",
    ],
)

cc_test(
    name = "do_fn_runner_test",
    srcs = ["do_fn_runner_test.cc"],
    deps = [
        ":do_fn_runner",
        ":records",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers:session",
        "@confidential-federated-compute//containers/fns",
        "@googletest//:gtest_main",
        "@protobuf//:protobuf",
    ],
)

cc_library(
    name = "ingest_uploads_router",
    srcs = ["ingest_uploads_router.cc"],
    hdrs = ["ingest_uploads_router.h"],
    deps = [
        ":records",
        "//src/proto:ingestion_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings:string_view",
        "@abseil-cpp//absl/types:span",
        "@confidential-federated-compute//containers:session",
        "@protobuf//:protobuf",
    ],
)

cc_test(
    name = "ingest_uploads_router_test",
    srcs = ["ingest_uploads_router_test.cc"],
    deps = [
        ":ingest_uploads_router",
        ":records",
        "//src/proto:common_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers:session",
        "@googletest//:gtest_main",
        "@protobuf//:protobuf",
    ],
)

cc_library(
    name = "ingestion_verification_router",
    srcs = ["ingestion_verification_router.cc"],
    hdrs = ["ingestion_verification_router.h"],
    deps = [
        ":records",
        "//src/proto:fingerprint_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
        "@confidential-federated-compute//containers:session",
    ],
)

cc_test(
    name = "ingestion_verification_router_test",
    srcs = ["ingestion_verification_router_test.cc"],
    deps = [
        ":ingestion_verification_router",
        ":records",
        "//src/proto:ingestion_cc_proto",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "threshold_filter_router",
    srcs = ["threshold_filter_router.cc"],
    hdrs = ["threshold_filter_router.h"],
    deps = [
        ":records",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
        "@confidential-federated-compute//containers:session",
    ],
)

cc_test(
    name = "threshold_filter_router_test",
    srcs = ["threshold_filter_router_test.cc"],
    deps = [
        ":records",
        ":threshold_filter_router",
        ":scatter_shuffle_router",
        "//src/proto:common_cc_proto",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@googletest//:gtest_main",
        "@protobuf//:protobuf",
    ],
)

cc_library(
    name = "pre_padding_router",
    srcs = ["pre_padding_router.cc"],
    hdrs = ["pre_padding_router.h"],
    deps = [
        ":records",
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers:session",
    ],
)

cc_library(
    name = "pipeline_runner",
    srcs = ["pipeline_runner.cc"],
    hdrs = ["pipeline_runner.h"],
    deps = [
        ":do_fn_runner",
        ":ingest_uploads_router",
        ":ingestion_verification_router",
        ":pre_padding_router",
        ":records",
        ":threshold_filter_router",
        ":scatter_shuffle_router",
        "//src/containers/ingest_uploads:ingest_uploads_fn",
        "//src/containers/ingestion_verification:ingestion_verification_fn",
        "//src/containers/run_context_initializer:run_context_initializer_fn",
        "//src/containers/threshold_filter:threshold_filter_fn",
        "//src/containers/pre_padding:pre_padding_fn",
        "//src/containers/scatter_shuffle:scatter_shuffle_fn",
        "//src/proto:config_constraints_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "//src/proto:run_context_cc_proto",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@oak//cc/crypto:signing_key",
    ],
)

cc_test(
    name = "shuffler_flow_integration_test",
    srcs = ["shuffler_flow_integration_test.cc"],
    deps = [
        ":ingest_uploads_router",
        ":ingestion_verification_router",
        ":pipeline_runner",
        ":pre_padding_router",
        ":records",
        ":scatter_shuffle_router",
        ":threshold_filter_router",
        "//src/proto:common_cc_proto",
        "//src/proto:config_constraints_cc_proto",
        "//src/proto:ingestion_cc_proto",
        "//src/proto:run_context_cc_proto",
        "//src/proto:trusted_shuffler_upload_cc_proto",
        "//src/testing:test_utils",
        "//src/util:metric_epoch_key_utils",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@confidential-federated-compute//containers:session",
        "@googletest//:gtest_main",
        "@oak//cc/crypto:signing_key",
    ],
)

cc_test(
    name = "pre_padding_router_test",
    srcs = ["pre_padding_router_test.cc"],
    deps = [
        ":records",
        ":pre_padding_router",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "scatter_shuffle_router",
    srcs = ["scatter_shuffle_router.cc"],
    hdrs = ["scatter_shuffle_router.h"],
    deps = [
        ":records",
        "//src/proto:common_cc_proto",
        "//src/util:metric_epoch_key_utils",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@confidential-federated-compute//containers:session",
    ],
)

cc_test(
    name = "scatter_shuffle_router_test",
    srcs = ["scatter_shuffle_router_test.cc"],
    deps = [
        ":records",
        ":scatter_shuffle_router",
        "//src/testing:test_utils",
        "//src/proto:common_cc_proto",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@googletest//:gtest_main",
        "@protobuf//:protobuf",
    ],
)
