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
    name = "fake_context",
    hdrs = ["fake_context.h"],
    deps = [
        "@confidential-federated-compute//containers:session",
        "@federated-compute//fcp/protos/confidentialcompute:confidential_transform_cc_proto",
        "@oak//cc/containers/sdk:encryption_key_handle",
        "@oak//cc/containers/sdk:signing_key_handle",
    ],
)

cc_library(
    name = "fake_keys",
    hdrs = ["fake_keys.h"],
    deps = [
        "@federated-compute//fcp/protos/confidentialcompute:confidential_transform_cc_proto",
        "@oak//cc/crypto:encryption_key",
        "@oak//cc/crypto:signing_key",
    ],
)

cc_library(
    name = "testable_transform",
    hdrs = ["testable_transform.h"],
    deps = [
        "@confidential-federated-compute//containers:session",
        "@federated-compute//fcp/protos/confidentialcompute:confidential_transform_cc_proto",
    ],
)

cc_test(
    name = "fake_context_test",
    srcs = ["fake_context_test.cc"],
    deps = [
        ":fake_context",
        "@googletest//:gtest_main",
    ],
)
