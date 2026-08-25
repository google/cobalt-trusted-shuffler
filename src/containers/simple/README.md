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

# Simple Container

The `simple` container is a proof-of-concept identity transform container that buffers incoming data payloads and emits them in encrypted form.

## Overview

This container implements the `ConfidentialTransformBase` / `Session` API. It serves as an identity transform stage:
- **Buffering**: Collects raw message payloads written during the session.
- **Emission**: Encrypts and emits all buffered blobs in order when the session is committed.

---

## Configuration

The container does not require configuration parameters. `Configure` returns an empty `ConfigureResponse`.

---

## Inputs & Outputs

### Inputs

#### Data Stream via `WriteRequest`
- **`unencrypted_data`** (`string`): The raw input payload bytes to be buffered.

---

### Outputs

#### Encrypted Messages (Emitted during `Commit`)
When `Commit` is invoked, the container iterates over all buffered payloads and emits them:
- **Key Index**: `0` (`kReencryptionKeyIndex`)
- **Value**: The raw payload encrypted with the session's re-encryption key.

---

## Data Visibility & Privacy Boundary

- **Confidential Enclave Execution**: The input payloads are processed inside the enclave.
- **Encrypted Emission**: All buffered payloads are emitted encrypted using `context.EmitEncrypted(0, blob)`, ensuring that data remains opaque to the untrusted host orchestrator.
