#!/bin/bash
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

# Extract GoB commit and Kokoro build ID for Bazel build stamping.
# In Kokoro CI, KOKORO_GIT_COMMIT and KOKORO_BUILD_NUMBER are exported.
# In local development builds, fallback to git rev-parse HEAD and 'local'.
GIT_COMMIT="${KOKORO_GIT_COMMIT:-$(git rev-parse HEAD 2>/dev/null || echo 'unknown')}"
BUILD_ID="${KOKORO_BUILD_NUMBER:-local}"

echo "STABLE_GIT_COMMIT ${GIT_COMMIT}"
echo "STABLE_BUILD_ID ${BUILD_ID}"
