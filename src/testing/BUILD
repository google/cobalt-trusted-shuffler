load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fake_context",
    hdrs = ["fake_context.h"],
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