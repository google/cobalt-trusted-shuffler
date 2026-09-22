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

# Pre-Padding Container (`pre_padding`)

The **`pre_padding`** container generates encrypted dummy padding messages for approved metric-epoch partitions so that each partition's total message count is rounded up to a multiple of `batch_num` before entering the two-pass shuffle.

---

## 1. Purpose & Role in Pipeline

After `threshold_filter` drops unapproved partitions, each surviving partition has a message count `M` recorded in the canonical ingestion fingerprint. To enable balanced batching across `batch_num = ceil(M / target_batch_size)` scatter shuffle batches, `pre_padding` computes `padding_count` for each approved partition using `ComputeBatchParametersForPartition` and emits `padding_count` dummy `ShuffledMessage` records marked with `is_padding = true`.

---

## 2. Configuration & Inputs

- **Record 0 (`OnConfig`)**: Serialized `IngestionVerificationPayload` (`src/proto/ingestion.proto`) decrypted under `Node::kVerifiedIngestion`, validated via `VerifyIngestionVerificationPayload` to extract the `VerifiedIngestionVerificationResult` (`run_context`, `approved_partitions`, and `canonical_fingerprint`).
- **Records 1..N (`OnRecord`)**: Not accepted (`pre_padding` is a configuration-driven generator stage; any data record passed to `OnRecord` triggers an `IntegrityFault`).

---

## 3. Outputs & Privacy Boundary

- **Finalize Emission (`FinalizeReplica`)**:
  - **Encrypted Padding Messages (`EmitEncrypted` to `Node::kPaddingMessages`, key index 0)**:
    - **Plaintext Key (`KV.key`)**: `google.protobuf.Any` packing `MetricEpochKey` (`project_id`, `metric_id`, `epoch_index`) so the untrusted orchestrator can route padding messages alongside filtered real messages into the corresponding partition's scatter shuffle batches.
    - **Encrypted Value (`KV.data`)**: Serialized `ShuffledMessage` (`metric_epoch_key`, `run_id`, `is_padding = true`) encrypted under the `Node::kPaddingMessages` re-encryption key. The `is_padding` flag and `run_id` are completely opaque to the untrusted orchestrator.
- **Integrity Faults (`EmitUnencrypted`)**:
  - If `IngestionVerificationPayload` validation fails, a data record is received in `OnRecord`, or `FinalizeReplica` is invoked before configuration is established, the container emits a single unencrypted `IntegrityFault` message (`src/proto/common.proto`) and returns `FAILED_PRECONDITION`.
