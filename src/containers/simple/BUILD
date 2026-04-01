load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@oak//bazel:defs.bzl", "oci_runtime_bundle")
load("@rules_oci//oci:defs.bzl", "oci_image")
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")

cc_binary(
    name = "main",
    srcs = ["main.cc"],
    deps = [
        ":simple_transform",
        "@abseil-cpp//absl/log",
        "@abseil-cpp//absl/log:check",
        "@oak//cc/containers/sdk:encryption_key_handle",
        "@oak//cc/containers/sdk:orchestrator_client",
        "@oak//cc/containers/sdk:signing_key_handle",
        "@oak//proto/containers:orchestrator_crypto_cc_grpc",
    ],
)

cc_library(
    name = "simple_session",
    srcs = ["simple_session.cc"],
    hdrs = ["simple_session.h"],
    deps = [
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers:session",
        "@federated-compute//fcp/protos/confidentialcompute:confidential_transform_cc_proto",
    ],
)

cc_library(
    name = "simple_transform",
    srcs = ["simple_transform.cc"],
    hdrs = ["simple_transform.h"],
    deps = [
        ":simple_session",
        "@abseil-cpp//absl/status:statusor",
        "@confidential-federated-compute//containers:confidential_transform_server_base",
        "@confidential-federated-compute//containers:session",
        "@federated-compute//fcp/protos/confidentialcompute:confidential_transform_cc_proto",
    ],
)

cc_test(
    name = "simple_session_test",
    srcs = ["simple_session_test.cc"],
    deps = [
        ":simple_session",
        "//src/testing:fake_context",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:status_matchers",
        "@abseil-cpp//absl/status:statusor",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "simple_transform_test",
    srcs = ["simple_transform_test.cc"],
    deps = [
        ":simple_transform",
        "//src/testing:fake_context",
        "//src/testing:fake_keys",
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
