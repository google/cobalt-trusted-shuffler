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

# Threshold Filter Container (`threshold_filter`)

The `threshold_filter` container filters partitions and strips privacy identifiers in the Trusted Shuffler confidential data pipeline. Running inside a hardware Trusted Execution Environment (TEE), it verifies ingestion results, filters out partitions that failed to meet device privacy thresholds, and emits encrypted customer message streams for downstream two-pass shuffling stages.

---

## 1. Overview & Pipeline Role

In the Trusted Shuffler architecture:
1. **`ingestion_verification`**: Calculates device cardinality sketches and evaluates privacy thresholds, producing an `IngestionVerificationPayload` containing an `IngestionVerificationResult` with a `ThresholdReport` of approved `MetricEpochKey` partitions and the `SignedRunContext`.
2. **`GroupByPartitionKey` (Partition Grouping)**: Groups partially-encrypted `ShuffledMessage` streams by unencrypted `MetricEpochKey`.
3. **`threshold_filter`**: Consumes the single configuration payload `IngestionVerificationPayload` on record 0. It extracts and verifies `SignedRunContext` from `IngestionVerificationResult`, validates the run ID against `ThresholdReport.run_id`, checks each incoming message's partition key against the approved partitions list, drops any partitions below threshold, and re-encrypts approved messages to the `kFilteredMessages` data node for downstream shuffling stages. *(Note: sensitive `privacy_id` tokens were already discarded during client upload ingestion and are omitted from `ShuffledMessage`)*.

---

## 2. Configuration & Initialization

The container uses `FirstRecordConfigDoFn<IngestionVerificationPayload>`:
- **`IngestionVerificationPayload`** ([`src/proto/ingestion.proto`](../../proto/ingestion.proto)):
  - Ingested on record 0 as the single configuration record via `FirstRecordConfigDoFn::OnConfig`, validated via `VerifyIngestionVerificationPayload` to obtain the `VerifiedIngestionVerificationResult`, from which it caches `run_context` and `approved_partitions`.

---

## 3. Data Inputs & Outputs

### Inputs (`Do`)

1. **Record 0: Configuration (`IngestionVerificationPayload`)**:
   - Ingests and verifies the execution configuration proto and approved partition report.
   - If `IngestionVerificationPayload` validation fails, the container emits a single unencrypted `IntegrityFault` message (`src/proto/common.proto`) and returns `FAILED_PRECONDITION`.

2. **Records 1..N: Grouped Messages Stream (from Partition Grouping `GroupByMetricEpochKey`)**:
   - **Key**: Stripped/empty (or optional `MetricEpochKey` header).
   - **Data**: Serialized `ShuffledMessage` (decrypted by Oak runtime via key 2).
   - **Validation**:
     - `ShuffledMessage.run_id == RunContext.run_id`.
     - `ShuffledMessage.shuffle_count == 0`.
     - If header key is provided, verifies `header_key == ShuffledMessage.metric_epoch_key`.
   - **Behavior**: If the partition `ShuffledMessage.metric_epoch_key` is in `ThresholdReport.approved_partitions`, it is re-encrypted to the `kFilteredMessages` data node and emitted with `MetricEpochKey` as the clear key. If the partition is not approved, the message is discarded.

---

## 4. Outputs

- **Emission Primitive**:
  - `EmitEncrypted(context, key, message)` for approved messages, which are encrypted to the `kFilteredMessages` data node.
  - `context.EmitUnencrypted(kv)` for `IntegrityFault` records upon invariant violations.
- **Key**: `MetricEpochKey` identifying the partition (plaintext to untrusted orchestrator for routing), or `IntegrityFault` for integrity faults.
- **Data**: Serialized `ShuffledMessage` encrypted under key 0 (opaque to untrusted orchestrator; readable only by downstream shuffling stages), or serialized `IntegrityFault` in plaintext.
- **Partitions below threshold**: Dropped completely. No output records or metadata are emitted.
- **Privacy Boundary Rule**: `EmitUnencrypted` is **never** invoked for message payloads; only non-sensitive `IntegrityFault` control records are permitted via `EmitUnencrypted` upon integrity violations.

---

## 4. Trust Boundary Diagram

```
+-----------------------------------------------------------------------------------+
|                              Untrusted Orchestrator                               |
|   - Inspects MetricEpochKey routing metadata                                      |
|   - CANNOT read ciphertext from EmitEncrypted                                     |
+----------------------------------------+------------------------------------------+
                                         |
                   Encrypted KV Pairs (to kFilteredMessages)
                                         |
                                         v
+-----------------------------------------------------------------------------------+
|                     Downstream TEE Stage (Scatter Shuffle)                        |
|   - Holds session decryption key                                                  |
|   - Decrypts and shuffles messages inside hardware enclave                        |
+-----------------------------------------------------------------------------------+
```
