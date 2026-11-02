<!--
 Copyright 2026 Google LLC.

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
-->

# Trusted Shuffler

This repo contains the necessary elements to run Cobalt's Trusted Shuffler.

This is not an officially supported Google product.

## Set up

### Bazel

This project uses a specific version of Bazel (defined in [.bazelversion](.bazelversion)). It is
recommended to use [Bazelisk](https://github.com/bazelbuild/bazelisk) to manage your Bazel versions.

### Additional libraries

The following dependencies are also needed for development:

-  libxml2

They can be installed through apt-get:

```bash
sudo apt-get install libxml2
```

## Building containers

To build a single container runtime bundle (for example, `encrypt_output`):

```bash
bazelisk build -c opt //src/containers/encrypt_output:oci_runtime_bundle
```

To build and export all release container runtime bundles to a directory:

```bash
bazelisk run -c opt //:install_release_binaries -- --destdir=/tmp/binaries
```

## Running tests

To run the shuffler tests, use the following command:

```bash
bazel test //...
```

## Regenerating compile comands

To regenerate `compile_commands.json`, use the following command:

```bash
bazel run //:refresh_compile_commands
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines.

## Security

Eligibility for the [Google Open Source Software Vulnerability Rewards Program](https://bughunters.google.com/open-source-security) is determined by the [Google Open Source Software Vulnerability Reward Program Rules](https://bughunters.google.com/about/rules/open-source/google-open-source-software-vulnerability-reward-program-rules).
