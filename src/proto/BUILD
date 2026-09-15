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

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "trusted_shuffler_upload_proto",
    srcs = ["trusted_shuffler_upload.proto"],
)

cc_proto_library(
    name = "trusted_shuffler_upload_cc_proto",
    deps = [":trusted_shuffler_upload_proto"],
)

proto_library(
    name = "run_context_proto",
    srcs = ["run_context.proto"],
)

cc_proto_library(
    name = "run_context_cc_proto",
    deps = [":run_context_proto"],
)

proto_library(
    name = "config_constraints_proto",
    srcs = ["config_constraints.proto"],
)

cc_proto_library(
    name = "config_constraints_cc_proto",
    deps = [":config_constraints_proto"],
)

proto_library(
    name = "common_proto",
    srcs = ["common.proto"],
)

cc_proto_library(
    name = "common_cc_proto",
    deps = [":common_proto"],
)

proto_library(
    name = "fingerprint_proto",
    srcs = ["fingerprint.proto"],
    deps = [":common_proto"],
)

cc_proto_library(
    name = "fingerprint_cc_proto",
    deps = [":fingerprint_proto"],
)

proto_library(
    name = "sketch_proto",
    srcs = ["sketch.proto"],
)

cc_proto_library(
    name = "sketch_cc_proto",
    deps = [":sketch_proto"],
)

proto_library(
    name = "ingestion_proto",
    srcs = ["ingestion.proto"],
    deps = [
        ":common_proto",
        ":fingerprint_proto",
        ":run_context_proto",
        ":sketch_proto",
    ],
)

cc_proto_library(
    name = "ingestion_cc_proto",
    deps = [":ingestion_proto"],
)

proto_library(
    name = "release_proto",
    srcs = ["release.proto"],
    deps = [
        ":common_proto",
        ":fingerprint_proto",
    ],
)

cc_proto_library(
    name = "release_cc_proto",
    deps = [":release_proto"],
)

proto_library(
    name = "scatter_shuffle_proto",
    srcs = ["scatter_shuffle.proto"],
    deps = [":common_proto"],
)

cc_proto_library(
    name = "scatter_shuffle_cc_proto",
    deps = [":scatter_shuffle_proto"],
)

cc_test(
    name = "debug_redact_test",
    srcs = ["debug_redact_test.cc"],
    deps = [
        ":common_cc_proto",
        ":fingerprint_cc_proto",
        ":ingestion_cc_proto",
        ":release_cc_proto",
        ":run_context_cc_proto",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
