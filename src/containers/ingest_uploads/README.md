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

# Ingest Uploads Container

The `ingest_uploads` container is the confidential data transformation stage responsible for ingesting device uploads in the Trusted Shuffler.

## Overview

This container implements the `DoFn` / `FnConfidentialTransform` API for the First Ingestion Stage.

It parses client upload checkpoints whose upload entries are `TrustedShufflerUpload` messages, validates each entry's metric identifier and epoch against a `SignedRunContext`, updates per-partition HyperLogLog (`HllSketch`) sketches and `MultisetFingerprint`, and emits encrypted `ShuffledMessage` records and `PartialIngestionReport` outputs.

---

## Configuration & Key Indices

- **`Node::kIngestedMessages`** (access policy node 2): Used for emitted encrypted messages.
- **`Node::kPartialIngestionReports`** (access policy node 3): Used for emitted partial ingestion reports.

---

## Integrity Faults

If the `SignedRunContext` on record 0 fails verification, or configuration was never established
before finalization, the container emits a single **unencrypted** record keyed by an
`IngestMessagesHeader` with `integrity_fault` set and an empty `IntegrityFault` payload, increments
`integrity_fault-count`, and aborts the session with `FAILED_PRECONDITION`. No `ShuffledMessage` or
`PartialIngestionReport` is emitted for a faulted replica. The fault record never contains
`run_id`, `run_hash_key`, `release_key`, or any client data.

---

## Counters

The container reports these counters through the session metrics. The Brella host wrapper exports
them as Flume counters prefixed with the stage name (e.g. `IngestUploads-input_record-dropped`).
Counters are aggregate totals only; they never carry metric, epoch, or payload details.

| Counter | Unit | Meaning |
| :------ | :--- | :------ |
| `input_config-count` | config | `SignedRunContext` records received. |
| `input_config-invalid` | config | `SignedRunContext` records that failed verification. |
| `input_record-count` | record | Client upload records received after the config. |
| `input_record-dropped` | mixed | Sum of all drop reasons below (kept for compatibility). |
| `input_record-dropped-parse_error` | record | Records whose checkpoint could not be parsed (e.g. missing or empty entries, invalid `TrustedShufflerUpload`, missing or invalid event times). |
| `input_record-dropped-missing_privacy_id` | record | Records without a `confidential_compute_privacy_id`. |
| `upload_entry-count` | entry | `TrustedShufflerUpload` entries in successfully parsed records. |
| `upload_entry-dropped-unknown_metric` | entry | Entries whose `(project_id, metric_id)` is not in the `RunConfig`. |
| `upload_entry-dropped-out_of_window` | entry | Entries whose event-time epoch is outside `[min_epoch_index, max_epoch_index]`. |
| `encrypted_output_record-count` | record | Encrypted `ShuffledMessage`s plus the `PartialIngestionReport`. |
| `integrity_fault-count` | record | Integrity faults emitted. |
