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

load("@protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@protobuf//bazel:proto_library.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "counter_config_proto",
    srcs = ["counter_config.proto"],
)

cc_proto_library(
    name = "counter_config_cc_proto",
    deps = [":counter_config_proto"],
)

proto_library(
    name = "count_report_proto",
    srcs = ["count_report.proto"],
)

cc_proto_library(
    name = "count_report_cc_proto",
    deps = [":count_report_proto"],
)

proto_library(
    name = "output_key_proto",
    srcs = ["output_key.proto"],
)

cc_proto_library(
    name = "output_key_cc_proto",
    deps = [":output_key_proto"],
)
