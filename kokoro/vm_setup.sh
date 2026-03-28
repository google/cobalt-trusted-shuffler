#!/bin/bash

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

# Update apt repository list.
log "Updating apt."
apt-get update

# Install tooling dependencies.
#  - git is required by hedron_compile_commands.
#  - python is required for oci image generation.
log "Installing tooling dependencies."
apt-get install -y git python3 python-is-python3

# Install build dependencies.
#  - libxml2 is required by hedron_compile_commands.
log "Installing build dependencies."
apt-get install -y libxml2

log "Setting up bazel"
BAZEL_VERSION=bazel-8.5.1-linux-x86_64
mkdir -p /tmpfs/tmp/bazel-release
ln -fs "${KOKORO_GFILE_DIR}/${BAZEL_VERSION?}" /tmpfs/tmp/bazel-release/bazel
chmod 755 "${KOKORO_GFILE_DIR}/${BAZEL_VERSION?}"
export PATH="/tmpfs/tmp/bazel-release:${PATH}"
# This should show /tmpfs/tmp/bazel-release/bazel
which bazel

export SILO_KEY=ci
