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

The `encrypt_output` container is the confidential data transformation stage responsible for encrypting shuffled messages with the run's release key (`RunContext.release_key`), accumulating the partial multiset release fingerprint, and emitting the encrypted output messages and partial release report.

## Overview

This container implements the `DoFn` / `FnConfidentialTransform` API for output encryption and release fingerprinting per b/557302528.

In the Trusted Shuffler pipeline, the `encrypt_output` container processes shuffled customer messages after two-pass shuffling. It validates `IngestionVerificationPayload` on Record 0 (extracting `IngestionVerificationResult`, verifying `SignedRunContext` via `VerifySignedRunContext`, and extracting `ThresholdReport` to establish `approved_partitions`), checks that messages have completed both shuffling passes (`shuffle_count == 2`), match the active `run_id`, and belong to an approved partition, encrypts non-padding customer message payloads for release using `ReleaseEncrypter` (AES-256-GCM via Tink AEAD initialized from `RunContext.release_key`), accumulates message hashes into a partial release multiset fingerprint (`MultisetFingerprint`), and emits encrypted output records (`EncryptedMessageOutput`). During replica finalization, it emits a `PartialReleaseReport` containing the accumulated `partial_fingerprint` encrypted to the downstream release verification stage.

---

## Inputs & Outputs

### Inputs
- **`IngestionVerificationPayload` (Record 0)**: Ingested on Record 0; contains `IngestionVerificationResult` with `SignedRunContext` (verified via `VerifySignedRunContext` to extract `run_id`, `run_hash_key`, `run_config`, and `release_key` and initialize `ReleaseEncrypter` and `MultisetFingerprint`) and `ThresholdReport` (specifying `approved_partitions`).
- **Shuffled Messages (Records 1..N)**: Shuffled customer messages (`ShuffledMessage`) in the record payload (`record.data`), with `shuffle_count == 2`, `run_id` matching the active run, and `metric_epoch_key` belonging to `approved_partitions`. Padding messages (`is_padding == true`) are silently dropped after invariant validation.

### Outputs
- **Streaming Messages (`EmitUnencrypted`)**: `EncryptedMessageOutput` containing `MetricEpochKey` and customer payloads encrypted for release using `ReleaseEncrypter` with `RunContext.release_key`. Opaque to the untrusted orchestrator until the release key is released.
- **Finalize Emission (`EmitEncrypted`)**: `PartialReleaseReport` containing the accumulated `partial_fingerprint` emitted encrypted under key index 0 (`kReencryptionKeyIndex`) to the downstream release verification stage.

---

## Configuration & Key Indices

- **Transform Configuration**: None currently (accepts empty `google.protobuf.Any`).
- **Input Decryption**: Inputs are decrypted by the framework using the container's private encryption key handle.
- **Output Encryption**:
  - Customer message payloads are encrypted for release using `ReleaseEncrypter` (Tink AES-256-GCM AEAD with `RunContext.release_key`) and emitted via `EmitUnencrypted`.
  - Finalization payload `PartialReleaseReport` is emitted via `EmitEncrypted` with key index 0 (`kReencryptionKeyIndex`) for downstream release verification.


