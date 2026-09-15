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

# Encrypt Output Container

The `encrypt_output` container is the confidential data transformation stage responsible for encrypting shuffled messages with a per-worker release key ("encrypt-for-release"), accumulating the partial multiset release fingerprint, and emitting the encrypted output messages and release key verification material.

## Overview

This container implements the `DoFn` / `FnConfidentialTransform` API for output encryption and release fingerprinting per b/557302528.

In the Trusted Shuffler pipeline, the `encrypt_output` container processes shuffled customer messages after two-pass shuffling. It validates that messages have completed both shuffling passes (`shuffle_count == 2`), match the active `run_id`, and belong to approved partitions specified in the ingestion verification threshold report. It generates a worker-specific release key (AES-256-GCM via Tink AEAD), encrypts customer message payloads for release ("encrypt-for-release"), accumulates message counts into a release multiset fingerprint (ignoring padding messages), and emits encrypted output records. During replica finalization, it packages the accumulated release fingerprint and worker release key into a `WorkerKeyAndFingerprint` proto emitted encrypted to the downstream key release verification stage.

**Note:** This container currently serves as an empty skeleton. Its `DoFn` is a no-op placeholder that returns OK status without processing inputs.
TODO(b/557302528): Implement record routing, invariant validation, Tink AES-256-GCM message encryption for release, multiset fingerprint accumulation, and release key emission logic.

---

## Inputs & Outputs

### Inputs
- **`IngestionVerificationResult`**: Ingested on record 0 (containing `SignedRunContext` and approved partitions from the `ThresholdReport`) to establish `run_id` and initialize the release multiset fingerprint.
- **Shuffled Messages**: Shuffled customer messages (`ShuffledMessage`) with `shuffle_count == 2` from the two-pass shuffle stage.

### Outputs
- **Streaming Messages (`EmitUnencrypted`)**: `EncryptedMessageOutput` containing customer payloads encrypted for release under the worker's release key. Opaque to the untrusted orchestrator until release keys are released.
- **Finalize Emission (`EmitEncrypted`)**: `WorkerKeyAndFingerprint` emitted encrypted under key index 0 to the downstream key release verification stage.

---

## Configuration & Key Indices

- **Transform Configuration**: None currently (accepts empty `google.protobuf.Any`).
- **Input Decryption**: Inputs are decrypted by the framework using the container's private encryption key handle.
- **Output Encryption**:
  - Customer message payloads are encrypted for release using a worker-generated AES-256-GCM release key and emitted via `EmitUnencrypted`.
  - Finalization payload `WorkerKeyAndFingerprint` is emitted via `EmitEncrypted` with key index 0 for downstream key release verification.

