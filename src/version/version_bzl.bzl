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

"""Macro for generating a version.bzl file containing the stamped git commit."""

def version_bzl(name = "version_bzl", **kwargs):
    """Generates a `version.bzl` file in the calling package.

    The generated file sets `GIT_VERSION` to the `STABLE_GIT_COMMIT` value
    emitted by `tools/workspace_status.sh`, falling back to "unknown" if the
    workspace status is unavailable. It is intended to be shipped alongside
    each container's `oci_runtime_bundle.tar` as an output artifact.

    Args:
      name: Name of the generated genrule target.
      **kwargs: Additional arguments forwarded to the underlying genrule.
    """
    native.genrule(
        name = name,
        outs = ["version.bzl"],
        cmd = """
GIT_COMMIT=""
if [ -f bazel-out/stable-status.txt ]; then
  GIT_COMMIT=$$(grep '^STABLE_GIT_COMMIT ' bazel-out/stable-status.txt 2>/dev/null | cut -d ' ' -f 2)
fi
echo "GIT_VERSION = \\"$${GIT_COMMIT:-unknown}\\"" > $@
""",
        stamp = 1,
        **kwargs
    )
