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

# If running in Kokoro, copy any potential output artifacts to the
# artifacts directory so that they can be found by the post-build
# upload artifact globs.
if [[ -n "${KOKORO_ARTIFACTS_DIR}" ]]; then
  log "Copying simple container output to kokoro artifacts dir."
  cp -Rp ./bazel-bin/src/containers/simple/* ${KOKORO_ARTIFACTS_DIR}/simple_container/.

  log "Copying run_context_initializer container output to kokoro artifacts dir."
  mkdir -p ${KOKORO_ARTIFACTS_DIR}/run_context_initializer
  cp -Rp ./bazel-bin/src/containers/run_context_initializer/* ${KOKORO_ARTIFACTS_DIR}/run_context_initializer/.

  log "Copying ingestion_verification container output to kokoro artifacts dir."
  mkdir -p ${KOKORO_ARTIFACTS_DIR}/ingestion_verification
  cp -Rp ./bazel-bin/src/containers/ingestion_verification/* ${KOKORO_ARTIFACTS_DIR}/ingestion_verification/.

  log "Copying ingest_uploads container output to kokoro artifacts dir."
  mkdir -p ${KOKORO_ARTIFACTS_DIR}/ingest_uploads
  cp -Rp ./bazel-bin/src/containers/ingest_uploads/* ${KOKORO_ARTIFACTS_DIR}/ingest_uploads/.

  log "Copying threshold_filter container output to kokoro artifacts dir."
  mkdir -p ${KOKORO_ARTIFACTS_DIR}/threshold_filter
  cp -Rp ./bazel-bin/src/containers/threshold_filter/* ${KOKORO_ARTIFACTS_DIR}/threshold_filter/.

  log "Copying pre_padding container output to kokoro artifacts dir."
  mkdir -p ${KOKORO_ARTIFACTS_DIR}/pre_padding
  cp -Rp ./bazel-bin/src/containers/pre_padding/* ${KOKORO_ARTIFACTS_DIR}/pre_padding/.

  log "Copying scatter_shuffle container output to kokoro artifacts dir."
  mkdir -p ${KOKORO_ARTIFACTS_DIR}/scatter_shuffle
  cp -Rp ./bazel-bin/src/containers/scatter_shuffle/* ${KOKORO_ARTIFACTS_DIR}/scatter_shuffle/.

  log "Copying gather_shuffle container output to kokoro artifacts dir."
  mkdir -p ${KOKORO_ARTIFACTS_DIR}/gather_shuffle
  cp -Rp ./bazel-bin/src/containers/gather_shuffle/* ${KOKORO_ARTIFACTS_DIR}/gather_shuffle/.

  log "Copying encrypt_output container output to kokoro artifacts dir."
  mkdir -p ${KOKORO_ARTIFACTS_DIR}/encrypt_output
  cp -Rp ./bazel-bin/src/containers/encrypt_output/* ${KOKORO_ARTIFACTS_DIR}/encrypt_output/.

  log "Copying key_releaser container output to kokoro artifacts dir."
  mkdir -p ${KOKORO_ARTIFACTS_DIR}/key_releaser
  cp -Rp ./bazel-bin/src/containers/key_releaser/* ${KOKORO_ARTIFACTS_DIR}/key_releaser/.

  # Kokoro tooling uses the `bazel_invocation_ids` file, but add a .txt
  # extension to it before also uploading it as an output artifact so
  # that the MIME type in GCS  gets set to something easily openable in
  # a browser.
  cp ${KOKORO_ARTIFACTS_DIR}/bazel_invocation_ids ${KOKORO_ARTIFACTS_DIR}/bazel_invocation_ids.txt
fi

