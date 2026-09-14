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

# Scatter Shuffle Container

The `scatter_shuffle` container is the confidential data transformation stage responsible for the scatter shuffle and group assignment phase (first pass) of the Trusted Shuffler two-pass shuffle.

## Overview

In the two-pass shuffling architecture, the scatter shuffle container processes batches of messages partitioned by `MetricEpochKey`:
1. **First Record Configuration**: Record 0 ingests an `IngestionVerificationPayload`, establishing the execution `run_id`, run configuration (`target_batch_size`, epoch bounds), approved partitions from `ThresholdReport`, and partition message counts from `canonical_fingerprint`.
2. **Batch Invariant Validation**: Records 1..N ingest `ShuffledMessage` records (both real encrypted customer messages and dummy padding messages). The container validates that every message shares an identical `MetricEpochKey`, matching `run_id`, and `shuffle_count == 0`. It computes batching parameters (`target_batch_size`, `batch_num`, `padding_count`) for the partition dynamically using the batch calculator library.
3. **Batch Size Confirmation**: The container verifies that the batch contains exactly `target_batch_size` messages. If any validation check, partition approval, or batch size expectation fails, it emits an unencrypted `IntegrityFault` to alert the untrusted orchestrator and halts session processing.
4. **Cryptographic Shuffling**: The verified batch is cryptographically shuffled using a Fisher-Yates CSPRNG algorithm (`src/crypto/shuffle.h`).
5. **Group Assignment & Re-encryption**: Each message is assigned a group identifier `group_id = new_position % batch_num`, has its `shuffle_count` incremented from 0 to 1, is re-encrypted under the stage's re-encryption key, and is emitted with a visible unencrypted `group_id` header.

---

## Inputs & Outputs

### Inputs
- **Record 0 (`IngestionVerificationPayload`)**: Contains `IngestionVerificationResult` (with the `SignedRunContext` (with `run_id`, secret keys, and `RunConfig` containing `target_batch_size`), the `ThresholdReport` (approved partitions), and the `canonical_fingerprint` (`FingerprintEntry` message counts per partition)) or an `IntegrityFault`.
- **Records 1..N (Batch Messages)**: `target_batch_size` messages with payload `ShuffledMessage`:
  - Real customer messages (`is_padding == false`).
  - Dummy padding messages (`is_padding == true`).

### Outputs
- **Streaming Output**: A stream of encrypted KV records (`ScatterShuffleHeader` with `group_id`, encrypted `ShuffledMessage`) emitted via `EmitEncrypted`:
  - Note: `kReencryptionKeyIndex = 0` is the container's local output node index passed to `EmitEncrypted` for the downstream gather shuffle stage.
  - **Header**: Visible unencrypted `ScatterShuffleHeader` with `group_id` (`new_position % batch_num`), inspected by the untrusted orchestrator to group messages for subsequent shuffle stages.
  - **Payload**: Re-encrypted `ShuffledMessage` ciphertext with `shuffle_count = 1`.
- **Integrity Violation Output**: Emitted via `EmitUnencrypted` only when an integrity violation occurs:
  - **Header**: `ScatterShuffleHeader` with `IntegrityFault`.
  - **Payload**: Serialized `IntegrityFault` proto.

---

## Data Visibility & Privacy Boundary

- **`EmitEncrypted(reencryption_key_index, kv)`**:
  - The `reencryption_key_index` argument (`0`) is the container's local output node index for the downstream gather shuffle stage.
  - The `group_id` header in `kv.key` is unencrypted and visible to the untrusted orchestrator for routing.
  - The `ShuffledMessage` payload in `kv.value` is encrypted with the session's re-encryption key and is **completely opaque** to the untrusted orchestrator. Only downstream confidential TEE stages holding the corresponding key can decrypt it.
- **`EmitUnencrypted(kv)`**:
  - Exclusively used to emit `IntegrityFault` signals upon validation or invariant failure.
  - **CRITICAL PRIVACY RULE**: No client records, individual identifiers, or unaggregated confidential data are ever emitted unencrypted.

---

## Configuration & Batch Calculation

The container determines its batch parameters dynamically from the `IngestionVerificationPayload` received on Record 0:
- **`target_batch_size`**: Target batch size configured in `SignedRunContext`.
- **`message_count`**: Total count of messages for the partition in `canonical_fingerprint`.
- **Batch Parameters**: Computed via the batch calculator library (`src/batching/batch_calculator.h`):
  - `batch_num = ceil(message_count / target_batch_size)`
  - `remainder_count = message_count % target_batch_size`
  - `padding_count = (target_batch_size - remainder_count) % target_batch_size`
