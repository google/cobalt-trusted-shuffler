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

The `ingestion_verification` container is a confidential data transform planned for verifying KMS signatures, aggregating run fingerprints, and evaluating device counts against reporting thresholds.

> [!NOTE]
> **This container does nothing right now.** It is currently an empty skeleton / no-op placeholder.
>
> <!-- TODO(b/555370722): Implement KMS signature verification, RunFingerprint aggregation, and device threshold evaluation. -->

## Overview

This container implements the `FnConfidentialTransform` / `DoFn` API to serve as the ingestion verification and threshold evaluation stage in a Trusted Shuffler confidential pipeline.

Currently, this container is a no-op placeholder that performs no transformation and emits no data.
TODO(b/555370722): Implement the container transform logic.

---

## Configuration & Constraints

### Configuration Protos
The container does not currently require custom runtime configuration parameters in `InitializeReplica`. During session initialization (`InitializeReplica`), the container logs the ingested `TrustedShufflerConfigConstraints` metric threshold combinations (project ID, metric ID, threshold) in plaintext via `LOG(INFO)`.

### Configuration Constraints
During transform initialization (`StreamInitializeTransform`), the transform ingests and validates `TrustedShufflerConfigConstraints` (defined in `src/proto/config_constraints.proto`) unpacked from `config_constraints`:
- Contains a list of `MetricThreshold` entries each specifying `project_id`, `metric_id`, and `threshold`.
- Validates that each `threshold`, `project_id`, and `metric_id` is positive.
- Disallows duplicate `(project_id, metric_id)` threshold definitions.
- The validated constraints are passed to `IngestionVerificationFnFactory` and attached to `IngestionVerificationFn` instances for device threshold evaluation.

---

## Inputs & Outputs

### Inputs

#### Data Stream via `Do`
- Incoming KV pairs from upstream stages. Currently a no-op that discards incoming inputs without processing.
- TODO(b/555370722): Process signed run contexts, signed access policies, and partial ingestion reports.

---

### Outputs

Currently, no outputs are emitted.
- TODO(b/555370722): Emit unencrypted summary header and encrypted verification payload containing approved partitions and canonical fingerprint.
