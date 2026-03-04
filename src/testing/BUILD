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

cc_test(
    name = "fake_context_test",
    srcs = ["fake_context_test.cc"],
    deps = [
        ":fake_context",
        "@googletest//:gtest_main",
    ],
)
