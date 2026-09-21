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

# Key Releaser Container (`key_releaser`)

## Purpose & Role in Pipeline

The **`key_releaser`** container performs cryptographic fingerprint verification and decryption key release at the end of the Trusted Shuffler pipeline. It acts as the final cryptographic privacy and integrity gate before the untrusted host orchestrator can decrypt shuffled customer messages.

Specifically, `key_releaser`:
1. Receives the verified `SignedRunContext` and canonical ingestion multiset fingerprint (`RunFingerprint`) emitted by the `ingestion_verification` container.
2. Aggregates partial release multiset fingerprints emitted by all `encrypt_output` container workers.
3. On replica finalization, verifies that the merged complete release multiset fingerprint matches the canonical ingestion fingerprint across all approved partitions using `MultisetFingerprint::VerifyComplete`.
4. Only when cryptographic equivalence holds, releases the run's Tink AES-256-GCM AEAD `release_key` to the untrusted orchestrator via `DecryptionKeyRelease`. If any fingerprint discrepancy, missing configuration, or upstream integrity fault is detected, it emits an `IntegrityFault` and never exposes `release_key`.

---

## Configuration & Input Stream

The container uses the first-record configuration pattern (`FirstRecordConfigDoFn<IngestionVerificationPayload>`):

- **Record 0 (`IngestionVerificationPayload`)**:
  - Must contain a valid `IngestionVerificationResult` with:
    - `SignedRunContext` containing the run's cryptographic parameters and `release_key`.
    - `ThresholdReport` specifying approved partitions (`MetricEpochKey`).
    - `canonical_fingerprint` (`RunFingerprint` with `stage_id = STAGE_INGESTION`, `is_partial = false`).
  - If `IngestionVerificationPayload` contains an upstream `IntegrityFault` or fails validation, the replica records an integrity fault and drains remaining records.

- **Records 1..N (`PartialReleaseReport`)**:
  - Each record contains a serialized `PartialReleaseReport` produced by an `encrypt_output` worker.
  - Each `PartialReleaseReport` contains a partial `RunFingerprint` (`stage_id = STAGE_RELEASE`, `is_partial = true`) representing the multiset fingerprint of customer messages encrypted by that worker.
  - The container merges each partial fingerprint into a complete release `MultisetFingerprint` accumulator.

---

## Output Stream & Finalize Emissions

No records are emitted during `Do()`. Upon `FinalizeReplica()`:

- **Success Path (`MultisetFingerprint::VerifyComplete` succeeds)**:
  - Emits a single KV pair via `context.EmitUnencrypted`:
    - **Key (`ReleaseOutputHeader`)**: `type = ReleaseOutputHeader::TYPE_REPORT`
    - **Value (`DecryptionKeyRelease`)**: `released_key_report.release_key` containing the serialized Tink AES-256-GCM AEAD keyset from `RunContext.release_key`.

- **Failure Path (Missing config, upstream fault, corrupt input, or fingerprint mismatch)**:
  - Emits a single KV pair via `context.EmitUnencrypted`:
    - **Key (`ReleaseOutputHeader`)**: `type = ReleaseOutputHeader::TYPE_INTEGRITY_FAULT`
    - **Value (`DecryptionKeyRelease`)**: `integrity_fault` set; `released_key_report` is omitted.

---

## Privacy & Secret Protection Invariants

- `run_id` and `run_hash_key` (`fingerprint_key`) are never emitted in unencrypted outputs, error messages, or logs.
- `release_key` is only ever emitted in `DecryptionKeyRelease.released_key_report.release_key` when `VerifyComplete` succeeds; it is never logged or included in error strings.
