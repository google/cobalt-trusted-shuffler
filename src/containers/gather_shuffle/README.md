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

# Gather Shuffle Container (`gather_shuffle`)

The **`gather_shuffle`** container performs the second pass of the Trusted Shuffler two-pass cryptographic shuffle.

---

## 1. Purpose & Role in Pipeline

After `scatter_shuffle` shuffles messages within per-partition batches and assigns each message a `group_id` via `GatherGroupIdForPosition`, the untrusted orchestrator groups scattered messages by their `ScatterGroupKey` (`MetricEpochKey` and `group_id`) and routes each group to a `gather_shuffle` replica.

Each `gather_shuffle` replica validates that all messages in the group belong to the same approved `(MetricEpochKey, group_id)` pair, have `shuffle_count == 1`, and match the expected gather group size derived via `ComputeExpectedGatherGroupSizeForPartition`. Upon finalization, it performs a cryptographic Fisher-Yates shuffle (`SecureShuffle`), increments `shuffle_count` to `2`, and emits the shuffled messages re-encrypted to `Node::kGatheredMessages` for `encrypt_output`.

---

## 2. Configuration & Inputs

- **Replica Initialization (`InitializeReplica`)**: Empty `google.protobuf.Any`.
- **Record 0 (`OnConfig`)**: Serialized `IngestionVerificationPayload` (`src/proto/ingestion.proto`) decrypted under `Node::kVerifiedIngestion`, validated via `VerifyIngestionVerificationPayload` to obtain the `VerifiedIngestionVerificationResult` (`run_context`, `approved_partitions`, and `canonical_fingerprint`).
- **Records 1..N (`OnRecord`)**: Serialized `ShuffledMessage` records (`src/proto/common.proto`) decrypted under `Node::kScatteredMessages`. Every message in the replica must share the same `metric_epoch_key`, `group_id`, and `run_id`, and have `shuffle_count == 1`.

---

## 3. Outputs & Privacy Boundary

- **Finalize Emission (`FinalizeReplica`)**:
  - **Encrypted Shuffled Messages (`EmitEncrypted` to `Node::kGatheredMessages`, key index 0)**:
    - **Plaintext Key (`KV.key`)**: Empty (`NoKey`).
    - **Encrypted Value (`KV.data`)**: Serialized `ShuffledMessage` with `shuffle_count = 2`, encrypted under the `Node::kGatheredMessages` re-encryption key for downstream `encrypt_output` workers.
- **Integrity Faults (`EmitUnencrypted`)**:
  - If `IngestionVerificationPayload` validation fails, any message fails `run_id`, `MetricEpochKey`, `group_id`, or `shuffle_count` checks, or the total number of messages in the replica does not match `expected_group_size`, the container emits a single unencrypted `IntegrityFault` message (`src/proto/common.proto`) and returns an error status.
