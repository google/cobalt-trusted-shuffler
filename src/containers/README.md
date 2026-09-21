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
| [`run_context_initializer`](run_context_initializer/) | Bootstraps the pipeline execution. Validates `RunConfig`, generates cryptographically secure `run_id`, `run_hash_key`, and `release_key`, and signs the `RunContext`. | Emits encrypted `SignedRunContext` to the `kRunContext` data node for downstream TEE stages. | [`src/proto/run_context.proto`](../proto/run_context.proto) |
| [`ingest_uploads`](ingest_uploads/) | Ingests device uploads, validates metric identifiers and epoch bounds against `SignedRunContext`, and updates HyperLogLog cardinality sketches and `MultisetFingerprint`. | Emits encrypted `ShuffledMessage` records to the `kIngestedMessages` data node and `PartialIngestionReport` outputs to `kPartialIngestionReports`. | [`src/proto/ingestion.proto`](../proto/ingestion.proto) |
| [`message_counter`](message_counter/) | Counts incoming messages while forwarding them. Outputs a final count summary report. | Emits messages encrypted (`OutputKey.type = MESSAGE`) and final `CountReport` unencrypted (`OutputKey.type = REPORT`). | [`src/proto/message_counter/`](../proto/message_counter/) |
| [`releaser`](releaser/) | Unconditionally releases messages passed from upstream confidential stages. | Emits messages unencrypted via `EmitUnencrypted` at the pipeline's privacy/release boundary. | None (empty config) |
| [`simple`](simple/) | Proof-of-concept identity transform. Buffers incoming blobs and forwards them. | Emits buffered blobs encrypted with key index 0 upon commit. | None (empty config) |
| [`ingestion_verification`](ingestion_verification/) | Verifies runtime configuration, merges partial multiset fingerprints and HyperLogLog cardinality sketches, and evaluates device counts against privacy thresholds. | Emits an unencrypted `IngestionVerificationHeader` labelling the payload variant (`ingestion_result` or `integrity_fault`) for the orchestrator, and an encrypted `IngestionVerificationPayload` to the `kVerifiedIngestion` data node for `ThresholdFilter`. | [`src/proto/config_constraints.proto`](../proto/config_constraints.proto), [`src/proto/ingestion.proto`](../proto/ingestion.proto) |
| [`threshold_filter`](threshold_filter/) | Filters messages by approved partition thresholds from Ingestion Verification report, drops unapproved partitions, and emits encrypted message streams. | Emits messages encrypted (`EmitEncrypted`) to the `kFilteredMessages` data node with plaintext `MetricEpochKey` headers. Partitions below threshold are dropped completely. | [`src/proto/ingestion.proto`](../proto/ingestion.proto) |
| [`scatter_shuffle`](scatter_shuffle/) | Cryptographically shuffles batches of messages partitioned by `MetricEpochKey`, validates batch size invariants, assigns group IDs (`group_id = new_position % batch_num`), increments `shuffle_count`, and re-encrypts payloads. | Emits encrypted KV records (`ScatterShuffleHeader` with `group_id` and encrypted `ShuffledMessage`) via `EmitEncrypted`; emits `IntegrityFault` via `EmitUnencrypted` on validation failure. | Record 0 `IngestionVerificationPayload` (`src/proto/ingestion.proto`) |
| [`encrypt_output`](encrypt_output/) | Encrypts shuffled messages for release using `RunContext.release_key` via `ReleaseEncrypter` and accumulates the partial multiset release fingerprint. | Emits streaming `EncryptedMessageOutput` unencrypted (payloads encrypted for release) and final `PartialReleaseReport` encrypted to the `kPartialReleaseReports` data node. | [`src/proto/run_context.proto`](../proto/run_context.proto), [`src/proto/release.proto`](../proto/release.proto) |
| [`key_releaser`](key_releaser/) | Aggregates partial release multiset fingerprints from `encrypt_output` workers and verifies complete multiset equivalence against the canonical ingestion fingerprint before releasing the run's Tink AEAD `release_key`. | Emits unencrypted `DecryptionKeyRelease` (`ReleasedKeyReport` or `IntegrityFault`) via `EmitUnencrypted` on replica finalization. | Record 0 `IngestionVerificationPayload` (`src/proto/ingestion.proto`), Records 1..N `PartialReleaseReport` (`src/proto/release.proto`) |

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

- **Purpose**: Parses client device uploads (`TrustedShufflerUpload` and `ClientPayload` input envelopes), validates metric identifiers and epoch bounds against `SignedRunContext`, updates per-partition HyperLogLog (`HllSketch`) sketches and `MultisetFingerprint`, and emits encrypted `ShuffledMessage` records and `PartialIngestionReport` outputs.
- **Documentation**: See [`src/containers/ingest_uploads/README.md`](ingest_uploads/README.md).

---

## 3. `message_counter`

The **`message_counter`** container counts messages during streaming execution and produces an unencrypted aggregation report upon finalization.

- **Purpose**: Forwards user message streams while maintaining a count of processed items.
- **Configuration**: `CounterConfig` containing a `counter_name`.
- **Inputs**: Encrypted message payloads processed via `Do`.
- **Outputs**:
  - **Streaming**: Emits each message encrypted with key `OutputKey(type = MESSAGE)`.
  - **Finalize**: Emits an unencrypted `CountReport` with key `OutputKey(type = REPORT)`.
- **Documentation**: See [`src/containers/message_counter/README.md`](message_counter/README.md).

---

## 4. `releaser`

The **`releaser`** container serves as the release stage for messages processed by upstream confidential stages.

- **Purpose**: Releases message payloads to the downstream consumer / untrusted orchestrator.
- **Configuration**: None.
- **Inputs**: Forwarded message payloads (e.g. from `message_counter`) with key `OutputKey(type = MESSAGE)`.
- **Outputs**: Emits message payloads unencrypted via `EmitUnencrypted`.
- **Documentation**: See [`src/containers/releaser/README.md`](releaser/README.md).

---

## 5. `simple`

The **`simple`** container is a proof-of-concept identity transform container.

- **Purpose**: Buffers incoming blobs and forwards them encrypted upon commit.
- **Configuration**: None.
- **Inputs**: Data payloads via `WriteRequest`.
- **Outputs**: Emits all buffered blobs encrypted with key index 0 upon receiving a `CommitRequest`.
- **Documentation**: See [`src/containers/simple/README.md`](simple/README.md).

---

## 6. `ingestion_verification`

The **`ingestion_verification`** container is a confidential transform responsible for verifying runtime configuration, aggregating multiset fingerprints, merging HyperLogLog sketches, and evaluating device counts against reporting thresholds.

- **Purpose**: Verifies incoming ingestion reports from `IngestUploads`, computes union device cardinality via merged HLL sketches, evaluates thresholds from `TrustedShufflerConfigConstraints`, and emits verification outputs.
- **Configuration**: `TrustedShufflerConfigConstraints` unpacked during transform initialization; `SignedRunContext` ingested on Record 0.
- **Inputs**: KV pairs processed via `Do` (Record 0 config, Records 1..N `PartialIngestionReport`).
- **Outputs**: Emits unencrypted `IngestionVerificationHeader` (`IngestionResult` or `IntegrityFault`) for the orchestrator, and encrypted `IngestionVerificationPayload` (approved partitions, canonical `RunFingerprint`, and `SignedRunContext`, or `IntegrityFault`) to the `kVerifiedIngestion` data node for `ThresholdFilter`.
- **Documentation**: See [`src/containers/ingestion_verification/README.md`](ingestion_verification/README.md).

---

## 7. `threshold_filter`

The **`threshold_filter`** container filters incoming partitions against approved privacy thresholds from Ingestion Verification and emits encrypted message streams for two-pass shuffling.

- **Purpose**: Drops unapproved partitions failing privacy thresholds, strips sensitive privacy IDs, and forwards encrypted customer messages to Scatter Shuffle.
- **Configuration**: `IngestionVerificationPayload` (record 0) containing `IngestionVerificationResult` (with `SignedRunContext`, `ThresholdReport`, and `RunFingerprint`).
- **Inputs**: `IngestionVerificationPayload` (record 0) and encrypted `ShuffledMessage` records (with header key stripped by Partition Grouping, or optional `MetricEpochKey` header) for records 1..N.
- **Outputs**: Emits `EncryptedKV<MetricEpochKey, ShuffledMessage, Node::kFilteredMessages>` with `MetricEpochKey` in plaintext for routing and the message payload encrypted to the `kFilteredMessages` data node via `EmitEncrypted`.
- **Documentation**: See [`src/containers/threshold_filter/README.md`](threshold_filter/README.md).

---

## 8. `scatter_shuffle`

The **`scatter_shuffle`** container is responsible for the scatter shuffle and group assignment phase (first pass) of the Trusted Shuffler two-pass shuffle.

- **Purpose**: Cryptographically shuffles batches of messages partitioned by `MetricEpochKey`, validates batch size invariants, assigns group IDs (`group_id = new_position % batch_num`), increments `shuffle_count` to 1, and re-encrypts payloads under the stage's re-encryption key.
- **Configuration**: Dynamic configuration via `IngestionVerificationPayload` on Record 0; computes batch parameters dynamically (`target_batch_size`, `actual_batch_size`, `batch_num`, `padding_count`) for the partition using `//src/batching:batch_calculator`.
- **Inputs**: Record 0 is `IngestionVerificationPayload`. Records 1..N are `actual_batch_size` messages with payload `ShuffledMessage` (real messages and dummy padding messages).
- **Outputs**: Emits encrypted KV records (`ScatterShuffleHeader` with `group_id` and encrypted `ShuffledMessage`) via `EmitEncrypted` with visible unencrypted `group_id` header and payload re-encrypted under the stage key. Emits `IntegrityFault` via `EmitUnencrypted` upon validation or batch size failure.
- **Documentation**: See [`src/containers/scatter_shuffle/README.md`](scatter_shuffle/README.md).

---

## 9. `encrypt_output`

The **`encrypt_output`** container is responsible for output encryption and release fingerprinting in the Trusted Shuffler pipeline.

- **Purpose**: Encrypts customer payloads under the run's release key (`RunContext.release_key`) using `ReleaseEncrypter` (AES-256-GCM AEAD), accumulates partial release multiset fingerprints, and emits encrypted output records and partial release reports.
- **Configuration**: Dynamic configuration via `IngestionVerificationPayload` on Record 0.
- **Inputs**: `IngestionVerificationPayload` on record 0 (containing `IngestionVerificationResult` with `SignedRunContext` and `ThresholdReport` with approved partitions), and `ShuffledMessage` records (`record.data`) with `shuffle_count == 2`.
- **Outputs**: Streaming `EncryptedMessageOutput` emitted unencrypted (payloads encrypted under `RunContext.release_key`), and `PartialReleaseReport` emitted encrypted to the `kPartialReleaseReports` data node for the downstream release verification stage upon finalization.
- **Documentation**: See [`src/containers/encrypt_output/README.md`](encrypt_output/README.md).

---

## 10. `key_releaser`

The **`key_releaser`** container performs cryptographic multiset fingerprint verification and decryption key release at the end of the pipeline.

- **Purpose**: Aggregates partial release multiset fingerprints from all `encrypt_output` workers, verifies complete equivalence against the canonical ingestion fingerprint (`MultisetFingerprint::VerifyComplete`), and releases the run's Tink AES-256-GCM AEAD `release_key` to the untrusted orchestrator only when verification succeeds.
- **Configuration**: `IngestionVerificationPayload` (record 0) containing `SignedRunContext`, `ThresholdReport`, and `canonical_fingerprint`.
- **Inputs**: `IngestionVerificationPayload` on record 0 and `PartialReleaseReport` records on records 1..N.
- **Outputs**: Emits a single unencrypted `DecryptionKeyRelease` (`ReleasedKeyReport` with `release_key`, or `IntegrityFault`) via `EmitUnencrypted` upon finalization.
- **Documentation**: See [`src/containers/key_releaser/README.md`](key_releaser/README.md).
