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

# Ingestion Verification Container

The `ingestion_verification` container is a confidential data transform in the Trusted Shuffler pipeline responsible for verifying runtime configuration, merging multiset fingerprints, aggregating HyperLogLog cardinality sketches across worker replicas, and evaluating unique device counts against configured reporting thresholds.

## Overview

This container implements the `FnConfidentialTransform` / `DoFn` API to serve as the ingestion verification stage in the Trusted Shuffler confidential pipeline.

It processes worker ingestion reports from `IngestUploads`, verifies integrity across the batch, determines which partitions met the minimum unique device privacy thresholds, and emits dual outputs for downstream consumption:
1. An unencrypted header (`IngestionVerificationHeader`) visible to the untrusted Flume orchestrator (exposes `ingestion_result` on success, or `integrity_fault` on validation/merging failure).
2. An encrypted payload (`IngestionVerificationPayload`) encrypted under key index 0 containing `ThresholdReport` (approved partitions), the canonical `RunFingerprint`, and `SignedRunContext`, readable only by downstream confidential TEE stages (`ThresholdFilter`).

---

## Configuration & Constraints

### Configuration Protos
During session initialization (`InitializeReplica`), the container logs the ingested `TrustedShufflerConfigConstraints` metric threshold combinations (project ID, metric ID, threshold) in plaintext via `LOG(INFO)`.

### Configuration Constraints
During transform initialization (`StreamInitializeTransform`), the transform ingests and validates `TrustedShufflerConfigConstraints` (defined in `src/proto/config_constraints.proto`) unpacked from `config_constraints`:
- Contains a list of `MetricThreshold` entries each specifying `project_id`, `metric_id`, and `threshold`.
- Validates that each `threshold`, `project_id`, and `metric_id` is positive.
- Disallows duplicate `(project_id, metric_id)` threshold definitions.
- The validated constraints are passed to `IngestionVerificationFnFactory` and attached to `IngestionVerificationFn` instances for device threshold evaluation.

---

## Inputs & Outputs

### Inputs

#### Streaming Input via `Do`
Inherits from `FirstRecordConfigDoFn<SignedRunContext>`:
- **Record 0 (Side-Input Configuration)**: Ingests `SignedRunContext` emitted by `run_context_initializer`. Unpacks and parses `RunContext` (validating non-empty `run_id`) and initializes a complete `MultisetFingerprint` with `stage_id = STAGE_INGESTION`.
- **Records 1..N (Ingestion Reports)**: Ingests serialized `PartialIngestionReport` messages from `IngestUploads` workers:
  - Validates matching `run_id`.
  - Merges `partial_fingerprint` into the cumulative multiset fingerprint.
  - Merges partial `hyperloglog_sketch` instances per `MetricEpochKey` partition.

---

### Outputs

Emitted exactly once during `FinalizeReplica` as `EncryptedKV<IngestionVerificationHeader, IngestionVerificationPayload, 0>`:

#### 1. Header (`IngestionVerificationHeader`) — Unencrypted
Packed in `KV.key` (`google::protobuf::Any`) and visible in plaintext to the untrusted Flume orchestrator. The `result` oneof explicitly labels which payload variant follows, and carries no partition data itself:
- `ingestion_result`: Empty marker indicating the payload contains an `IngestionVerificationResult` from successful verification.
- `integrity_fault`: Emitted if configuration failed, reports could not be parsed, `run_id` mismatched, or fingerprint/sketch merging failed.

#### 2. Payload (`IngestionVerificationPayload`) — Encrypted
Serialized in `KV.data` and encrypted with re-encryption key index 0 (readable by `ThresholdFilter`):
- `success`:
  - `threshold_report`: Contains `run_id` and the list of `approved_partitions` that met or exceeded reporting thresholds.
  - `canonical_fingerprint`: Canonical merged `RunFingerprint` (`is_partial = false`) containing `FingerprintEntry` records for approved partitions.
  - `signed_run_context`: The `SignedRunContext` received on record 0, passed through for downstream pipeline stages.
- `integrity_fault`: Emitted if any validation or merging failure occurred.
