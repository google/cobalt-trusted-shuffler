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

# Fail on any error.
set -e

# Display commands being run.
# WARNING: please only enable 'set -x' if necessary for debugging, and be very
#  careful if you handle credentials (e.g. from Keystore) with 'set -x':
#  statements like "export VAR=$(cat /tmp/keystore/credentials)" will result in
#  the credentials being printed in build logs.
#  Additionally, recursive invocation with credentials as command-line
#  parameters, will print the full command, with credentials, in the build logs.
# set -x

# Function for logging messages to the build output in a consistent format.
log() {
  echo "+ $1"
}

# Generate an invocation ID.
INVOCATION_ID=`cat /proc/sys/kernel/random/uuid`
if [[ -n "${KOKORO_ARTIFACTS_DIR}" ]]; then
  # If KOKORO_ARTIFACTS_DIR is set, append to the list of invocation ids.
  echo "${INVOCATION_ID}" >> ${KOKORO_ARTIFACTS_DIR}/bazel_invocation_ids
fi

log "Checking Apache 2.0 license headers."
bazel run @addlicense//:bin -- -check -l apache -c "Google LLC" \
  -ignore ".git/**" \
  -ignore "bazel-*/**" \
  -ignore "third_party/**" \
  "${PWD}"

log "Running bazel test."
bazel test //... \
  --spawn_strategy=dynamic \
  --strategy=Javac=dynamic \
  --strategy=Closure=dynamic \
  --genrule_strategy=dynamic \
  --google_default_credentials=true \
  --google_auth_scopes=https://www.googleapis.com/auth/cloud-platform \
  --remote_cache=${KOKORO_FOUNDRY_BACKEND_ADDRESS=remotebuildexecution.googleapis.com} \
  --remote_executor=remotebuildexecution.googleapis.com \
  --bes_backend=${KOKORO_BES_BACKEND_ADDRESS=buildeventservice.googleapis.com} \
  --bes_timeout=20s \
  --remote_timeout=20s \
  --project_id=cobalt-tee-shuffler \
  --remote_instance_name=projects/cobalt-tee-shuffler/instances/default_instance \
  --remote_local_fallback=true \
  --remote_retries=1 \
  --show_timestamps \
  --internal_spawn_scheduler \
  --strategy=Javac=dynamic \
  --dynamic_local_strategy=sandboxed,worker,local \
  --remote_accept_cached=true \
  --remote_default_exec_properties=cache-silo-key=${SILO_KEY=${USER}} \
  --invocation_id=${INVOCATION_ID}

RELEASE_CONTAINERS=(
  encrypt_output
  gather_shuffle
  ingest_uploads
  ingestion_verification
  key_releaser
  pre_padding
  run_context_initializer
  scatter_shuffle
  threshold_filter
)

# If running in Kokoro, copy any potential output artifacts to the
# artifacts directory so that they can be found by the post-build
# upload artifact globs.
if [[ -n "${KOKORO_ARTIFACTS_DIR}" ]]; then
  for container in "${RELEASE_CONTAINERS[@]}"; do
    log "Copying ${container} container output to kokoro artifacts dir."
    mkdir -p "${KOKORO_ARTIFACTS_DIR}/${container}"
    cp -Rp "./bazel-bin/src/containers/${container}/"* "${KOKORO_ARTIFACTS_DIR}/${container}/."
  done

  # Kokoro tooling uses the `bazel_invocation_ids` file, but add a .txt
  # extension to it before also uploading it as an output artifact so
  # that the MIME type in GCS  gets set to something easily openable in
  # a browser.
  cp ${KOKORO_ARTIFACTS_DIR}/bazel_invocation_ids ${KOKORO_ARTIFACTS_DIR}/bazel_invocation_ids.txt
fi

