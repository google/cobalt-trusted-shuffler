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

# Filter and Strip Privacy ID Container (`filter_and_strip_privacy_id`)

The `filter_and_strip_privacy_id` container is a confidential data transform in the Trusted Shuffler pipeline. It receives grouped messages and verification metadata, validates partition reporting thresholds against configured constraints, filters out partitions below threshold, and strips out `privacy_id` from messages before forwarding them downstream.

---

## Overview

Inside a hardware Trusted Execution Environment (TEE), this container performs three primary security and privacy functions:
1. **Run Context Verification**: Validates the cryptographic signature and execution parameters of `SignedRunContext` or `RunContext`.
2. **Threshold Verification & Filtering**: Aggregates message counts per partition (`MetricEpochKey`) and enforces metric reporting thresholds specified by `TrustedShufflerConfigConstraints`. Partitions failing to meet threshold requirements are dropped completely to uphold differential privacy guarantees.
3. **Privacy ID Stripping**: For passing partitions, strips the `privacy_id` field from all `ShuffledMessage` records before emitting them encrypted for downstream stages.

---

## Configuration

The container is configured during replica initialization:
- **`SignedRunContext`** / **`RunContext`** ([`src/proto/run_context.proto`](../../proto/run_context.proto)): Validates run parameters (`run_id`, `run_hash_key`, `metric_identifiers`, epoch range, batch size).
- **`TrustedShufflerConfigConstraints`** ([`src/proto/config_constraints.proto`](../../proto/config_constraints.proto)): Passed via access policy config constraints to specify `MetricThreshold` rules per `(project_id, metric_id)`.

---

## Inputs & Outputs

### Inputs

The container receives two types of encrypted KV inputs during `Do()`:

1. **Ingestion Verification Metadata (from Ingestion Verification stage)**:
   - **Key**: Header identifying the metric partition and run.
   - **Value**: Payload containing verified partition message counts and multiset fingerprint data.
2. **Grouped Messages Stream (from Partition Grouping stage)**:
   - **Key**: Partition key identifying the `MetricEpochKey`.
   - **Value**: Message payload containing customer message, partition key, and sensitive privacy identifier.

---

### Outputs

All emitted outputs are strictly **confidential and encrypted**:

- **Destination Node Key Index**: `6` (Downstream consumer).
- **Key**: Partition key identifying the `MetricEpochKey` (plaintext to untrusted orchestrator for routing).
- **Value**: Message serialized bytes encrypted using the session re-encryption key (`EmitEncrypted`). The sensitive privacy identifier is stripped.
- **Partitions below threshold**: Dropped entirely; no records or metadata are emitted.
- **Unencrypted Output**: None (`EmitUnencrypted` is **never** called for message data).

---

## Trust Boundary & Privacy Guarantees

```
+-----------------------------------------------------------------------------------+
|                              Untrusted Orchestrator                               |
|   - Inspects PartitionKey metadata for routing                                    |
|   - CANNOT read encrypted ShuffledMessage payloads                                |
+----------------------------------------+------------------------------------------+
                                         |
                            Encrypted KV Pairs (Key Index 6)
                                         |
                                         v
+-----------------------------------------------------------------------------------+
|                         Downstream Confidential TEE Stage                         |
|   - Holds session decryption key                                                  |
|   - Decrypts ShuffledMessage payloads (with privacy_id already stripped)          |
+-----------------------------------------------------------------------------------+
```
