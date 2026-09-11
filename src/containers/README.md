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

# Trusted Shuffler Containers

This directory contains confidential container implementations used in Trusted Shuffler pipelines running inside Trusted Execution Environments (TEEs) using Google's **Confidential Federated Compute (CFC)** framework and the **Oak Containers SDK**.

For instructions on developing, modifying, and contributing containers, see [.agents/AGENTS.md](../../.agents/AGENTS.md).

---

## Container Overview

| Container | Role & Purpose | Data Visibility & Emission | Protos / Config |
| :--- | :--- | :--- | :--- |
| [`run_context_initializer`](run_context_initializer/) | Bootstraps the pipeline execution. Validates `RunConfig`, generates cryptographically secure `run_id` and `run_hash_key`, and signs the `RunContext`. | Emits encrypted `SignedRunContext` (key index 0) for downstream TEE stages. | [`src/proto/run_context.proto`](../proto/run_context.proto) |
| [`ingest_uploads`](ingest_uploads/) | Placeholder skeleton; currently does nothing. TODO(b/555370722): Implement ingest uploads logic. | Emits nothing currently. | [`src/proto/ingestion.proto`](../proto/ingestion.proto) |
| [`message_counter`](message_counter/) | Counts incoming messages while forwarding them. Outputs a final count summary report. | Emits messages encrypted (`OutputKey.type = MESSAGE`) and final `CountReport` unencrypted (`OutputKey.type = REPORT`). | [`src/proto/message_counter/`](../proto/message_counter/) |
| [`releaser`](releaser/) | Unconditionally releases messages passed from upstream confidential stages. | Emits messages unencrypted via `EmitUnencrypted` at the pipeline's privacy/release boundary. | None (empty config) |
| [`simple`](simple/) | Proof-of-concept identity transform. Buffers incoming blobs and forwards them. | Emits buffered blobs encrypted with key index 0 upon commit. | None (empty config) |
| [`ingestion_verification`](ingestion_verification/) | Verifies runtime configuration, merges partial multiset fingerprints and HyperLogLog cardinality sketches, and evaluates device counts against privacy thresholds. | Emits unencrypted `IngestionVerificationHeader` for the orchestrator and encrypted `IngestionVerificationPayload` (key index 0) for `ThresholdFilter`. | [`src/proto/config_constraints.proto`](../proto/config_constraints.proto), [`src/proto/ingestion.proto`](../proto/ingestion.proto) |
| [`threshold_filter`](threshold_filter/) | Filters messages by approved partition thresholds from Ingestion Verification report, drops unapproved partitions, and emits encrypted message streams. | Emits messages encrypted (`EmitEncrypted`) with key index 0 and plaintext `MetricEpochKey` headers. Partitions below threshold are dropped completely. | [`src/proto/ingestion.proto`](../proto/ingestion.proto) |
| [`scatter_shuffle`](scatter_shuffle/) | Placeholder skeleton; currently does nothing. TODO(b/557296992): Implement scatter shuffle and group assignment logic. | Emits nothing currently. | None (accepts empty config) |
| [`encrypt_output`](encrypt_output/) | Placeholder skeleton; currently does nothing. TODO(b/557302528): Implement output encryption and release fingerprinting logic. | Emits nothing currently. | None (accepts empty config) |

---

## 1. `run_context_initializer`

The **`run_context_initializer`** container is executed **first** in the pipeline before any other processing stages.

- **Purpose**: Bootstraps the secure execution environment.
- **Inputs**: Serialized `RunConfig` protocol buffer (defining `metric_identifiers`, epoch range, and target batch size) received unencrypted via `WriteRequest`.
- **Outputs**: Emits a serialized `SignedRunContext` encrypted with the session re-encryption key. Downstream stages verify the signature against the attestation public key to ensure parameter integrity and prevent run confusion.
- **Documentation**: See [`src/containers/run_context_initializer/README.md`](run_context_initializer/README.md).

---

## 2. `ingest_uploads`

The **`ingest_uploads`** container is responsible for the First Ingestion Stage of device uploads in the Trusted Shuffler.

> [!NOTE]
> **This container currently serves as an empty skeleton.**
> TODO(b/555370722): Implement message validation, multiset fingerprinting, HLL sketch aggregation, and report generation logic.

- **Purpose**: First ingestion stage for device uploads (currently empty skeleton).
- **Documentation**: See [`src/containers/ingest_uploads/README.md`](ingest_uploads/README.md).

---

## 2. `message_counter`

The **`message_counter`** container counts messages during streaming execution and produces an unencrypted aggregation report upon finalization.

- **Purpose**: Forwards user message streams while maintaining a count of processed items.
- **Configuration**: `CounterConfig` containing a `counter_name`.
- **Inputs**: Encrypted message payloads processed via `Do`.
- **Outputs**:
  - **Streaming**: Emits each message encrypted with key `OutputKey(type = MESSAGE)`.
  - **Finalize**: Emits an unencrypted `CountReport` with key `OutputKey(type = REPORT)`.
- **Documentation**: See [`src/containers/message_counter/README.md`](message_counter/README.md).

---

## 3. `releaser`

The **`releaser`** container serves as the release stage for messages processed by upstream confidential stages.

- **Purpose**: Releases message payloads to the downstream consumer / untrusted orchestrator.
- **Configuration**: None.
- **Inputs**: Forwarded message payloads (e.g. from `message_counter`) with key `OutputKey(type = MESSAGE)`.
- **Outputs**: Emits message payloads unencrypted via `EmitUnencrypted`.
- **Documentation**: See [`src/containers/releaser/README.md`](releaser/README.md).

---

## 4. `simple`

The **`simple`** container is a proof-of-concept identity transform container.

- **Purpose**: Buffers incoming blobs and forwards them encrypted upon commit.
- **Configuration**: None.
- **Inputs**: Data payloads via `WriteRequest`.
- **Outputs**: Emits all buffered blobs encrypted with key index 0 upon receiving a `CommitRequest`.
- **Documentation**: See [`src/containers/simple/README.md`](simple/README.md).

---

## 5. `ingestion_verification`

The **`ingestion_verification`** container is a confidential transform responsible for verifying runtime configuration, aggregating multiset fingerprints, merging HyperLogLog sketches, and evaluating device counts against reporting thresholds.

- **Purpose**: Verifies incoming ingestion reports from `IngestUploads`, computes union device cardinality via merged HLL sketches, evaluates thresholds from `TrustedShufflerConfigConstraints`, and emits verification outputs.
- **Configuration**: `TrustedShufflerConfigConstraints` unpacked during transform initialization; `SignedRunContext` ingested on Record 0.
- **Inputs**: KV pairs processed via `Do` (Record 0 config, Records 1..N `PartialIngestionReport`).
- **Outputs**: Emits unencrypted `IngestionVerificationHeader` (summary message counts or `IntegrityFault`) for the orchestrator, and encrypted `IngestionVerificationPayload` (approved partitions, canonical `RunFingerprint`, and `SignedRunContext`, or `IntegrityFault`) under key index 0 for `ThresholdFilter`.
- **Documentation**: See [`src/containers/ingestion_verification/README.md`](ingestion_verification/README.md).

---

## 6. `threshold_filter`

The **`threshold_filter`** container filters incoming partitions against approved privacy thresholds from Ingestion Verification and emits encrypted message streams for two-pass shuffling.

- **Purpose**: Drops unapproved partitions failing privacy thresholds, strips sensitive privacy IDs, and forwards encrypted customer messages to Scatter Shuffle.
- **Configuration**: `IngestionVerificationPayload` (record 0) containing `IngestionVerificationResult` (with `SignedRunContext`, `ThresholdReport`, and `RunFingerprint`).
- **Inputs**: `IngestionVerificationPayload` (record 0) and `EncryptedKV<MetricEpochKey, ShuffledMessage, 2>` from Partition Grouping (records 1..N).
- **Outputs**: Emits `EncryptedKV<MetricEpochKey, ShuffledMessage, 0>` with `MetricEpochKey` in plaintext for routing and message payload encrypted under key 0 via `EmitEncrypted`.
- **Documentation**: See [`src/containers/threshold_filter/README.md`](threshold_filter/README.md).

---

## 7. `scatter_shuffle`

The **`scatter_shuffle`** container is responsible for Scatter Shuffle and Group Assignment in the Trusted Shuffler two-pass shuffle.

**Note:** This container currently serves as an empty skeleton. It is currently a no-op placeholder.
TODO(b/557296992): Implement message decryption, batch validation, cryptographic shuffling, group assignment, and payload re-encryption logic.

- **Purpose**: Shuffles batches of messages partitioned by `MetricEpochKey`, validates batch size and invariants, assigns group IDs (`group_id = new_position % batch_num`), increments `shuffle_count` to 1, and re-encrypts payloads under key 7.
- **Configuration**: None currently (accepts empty config).
- **Inputs**: `actual_batch_size` messages of `EncryptedKV<MetricEpochKey, ShuffledMessage, 6>` (including dummy padding messages from the pre-padding stage).
- **Outputs**: Emits `EncryptedKV<int32 group_id, ShuffledMessage, 7>` with unencrypted `group_id` header and payload re-encrypted under key 7.
- **Documentation**: See [`src/containers/scatter_shuffle/README.md`](scatter_shuffle/README.md).

---

## 8. `encrypt_output`

The **`encrypt_output`** container is responsible for output encryption and release fingerprinting in the Trusted Shuffler pipeline.

**Note:** This container currently serves as an empty skeleton. It is currently a no-op placeholder.
TODO(b/557302528): Implement output encryption, invariant validation, multiset fingerprint accumulation, and key release emission.

- **Purpose**: Encrypts customer payloads under a per-worker release key (AES-256-GCM), accumulates release multiset fingerprints, and emits encrypted payloads and key release material.
- **Configuration**: None currently (accepts empty config).
- **Inputs**: `IngestionVerificationResult` (containing `SignedRunContext` and approved partitions) on record 0, and `ShuffledMessage` records with `shuffle_count == 2`.
- **Outputs**: Streaming `EncryptedMessageOutput` emitted unencrypted (payloads encrypted under release key), and `WorkerKeyAndFingerprint` emitted encrypted under key index 0 to the downstream key release verification stage upon finalization.
- **Documentation**: See [`src/containers/encrypt_output/README.md`](encrypt_output/README.md).
