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

# Releaser Container

The `releaser` container is a confidential data transform that unconditionally releases the messages it receives.

## Overview

This container implements the `FnConfidentialTransform` / `DoFn` API to serve as the final release stage in a Trusted Shuffler confidential pipeline. It receives forwarded message records from upstream confidential stages (such as `message_counter`) and emits them in plaintext so that downstream consumers and the pipeline orchestrator can access the released data.

---

## Configuration

The container does not require custom runtime configuration parameters. `InitializeReplica` accepts an empty configuration proto.

---

## Inputs & Outputs

### Inputs

#### Data Stream via `Do`
- **Key**: A serialized `OutputKey` proto with `type = MESSAGE` (or empty if sent directly).
- **Value**: The message payload bytes.

---

### Outputs

#### Released Messages
For every message received in `Do`, the container emits the record unencrypted:
- **Emission Method**: `context.EmitUnencrypted(std::move(input))`
- **Key**: Preserves the incoming `OutputKey`.
- **Value**: The raw message payload in plaintext.

---

## Data Visibility & Privacy Boundary

The `releaser` container is positioned at the confidential pipeline's privacy boundary:
- **Upstream**: All intermediate data transformations operate inside confidential TEE enclaves and emit encrypted payloads (`EmitEncrypted`) that are completely opaque to the untrusted host orchestrator.
- **Release Stage**: The `releaser` enclave performs the final release action by emitting unencrypted records (`EmitUnencrypted`), making the final output available to the host orchestrator and external consumers.