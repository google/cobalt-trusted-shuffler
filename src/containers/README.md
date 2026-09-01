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
| [`message_counter`](message_counter/) | Counts incoming messages while forwarding them. Outputs a final count summary report. | Emits messages encrypted (`OutputKey.type = MESSAGE`) and final `CountReport` unencrypted (`OutputKey.type = REPORT`). | [`src/proto/message_counter/`](../proto/message_counter/) |
| [`releaser`](releaser/) | Unconditionally releases messages passed from upstream confidential stages. | Emits messages unencrypted via `EmitUnencrypted` at the pipeline's privacy/release boundary. | None (empty config) |
| [`simple`](simple/) | Proof-of-concept identity transform. Buffers incoming blobs and forwards them. | Emits buffered blobs encrypted with key index 0 upon commit. | None (empty config) |
| [`ingestion_verification`](ingestion_verification/) | Placeholder skeleton; currently does nothing. TODO(b/555370722): Implement KMS signature verification and device threshold evaluation. | Emits nothing currently. | None (accepts empty config) |

---

## 1. `run_context_initializer`

The **`run_context_initializer`** container is executed **first** in the pipeline before any other processing stages.

- **Purpose**: Bootstraps the secure execution environment.
- **Inputs**: Serialized `RunConfig` protocol buffer (defining `metric_identifiers`, epoch range, and target batch size) received unencrypted via `WriteRequest`.
- **Outputs**: Emits a serialized `SignedRunContext` encrypted with the session re-encryption key. Downstream stages verify the signature against the attestation public key to ensure parameter integrity and prevent run confusion.
- **Documentation**: See [`src/containers/run_context_initializer/README.md`](run_context_initializer/README.md).

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

The **`ingestion_verification`** container is a planned confidential transform responsible for KMS signature verification and device threshold evaluation.

> [!NOTE]
> **This container does nothing right now.** It is currently an empty skeleton / no-op placeholder.
> TODO(b/555370722): Implement signature verification, RunFingerprint aggregation, and device threshold evaluation.

- **Purpose**: Verifies incoming ingestion reports and checks device counts against required thresholds. (Currently does nothing).
- **Configuration**: None currently (accepts empty config).
- **Inputs**: KV pairs processed via `Do`. (Currently discards input).
- **Outputs**: Currently none (no-op implementation).
- **Documentation**: See [`src/containers/ingestion_verification/README.md`](ingestion_verification/README.md).