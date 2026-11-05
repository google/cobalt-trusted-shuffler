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

load("@hedron_compile_commands//:refresh_compile_commands.bzl", "refresh_compile_commands")
load("@rules_pkg//pkg:install.bzl", "pkg_install")
load("@rules_pkg//pkg:mappings.bzl", "pkg_files")

refresh_compile_commands(
    name = "refresh_compile_commands",
    targets = {
        "//...": "",
    },
)

# All release (i.e. production) container runtime bundles, along with their
# names in the destination directory.
_RELEASE_BINARIES = {
    # keep-sorted start
    "//src/containers/encrypt_output:oci_runtime_bundle": "encrypt_output/oci_runtime_bundle.tar",
    "//src/containers/gather_shuffle:oci_runtime_bundle": "gather_shuffle/oci_runtime_bundle.tar",
    "//src/containers/ingest_uploads:oci_runtime_bundle": "ingest_uploads/oci_runtime_bundle.tar",
    "//src/containers/ingestion_verification:oci_runtime_bundle": "ingestion_verification/oci_runtime_bundle.tar",
    "//src/containers/key_releaser:oci_runtime_bundle": "key_releaser/oci_runtime_bundle.tar",
    "//src/containers/pre_padding:oci_runtime_bundle": "pre_padding/oci_runtime_bundle.tar",
    "//src/containers/run_context_initializer:oci_runtime_bundle": "run_context_initializer/oci_runtime_bundle.tar",
    "//src/containers/scatter_shuffle:oci_runtime_bundle": "scatter_shuffle/oci_runtime_bundle.tar",
    "//src/containers/threshold_filter:oci_runtime_bundle": "threshold_filter/oci_runtime_bundle.tar",
    # keep-sorted end
}

pkg_files(
    name = "release_binaries",
    srcs = _RELEASE_BINARIES.keys(),
    renames = _RELEASE_BINARIES,
)

pkg_install(
    name = "install_release_binaries",
    srcs = [":release_binaries"],
)
