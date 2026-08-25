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

# Run Context Initializer Container

The `run_context_initializer` container is a confidential data transform that
takes a single `RunConfig` and produces an encrypted `SignedRunContext` to
bootstrap a secure pipeline execution.

## Overview

This container implements the `FnConfidentialTransform` / `DoFn` API to process an
incoming `RunConfig`. It validates the configuration parameters, generates
cryptographically secure identifiers (`run_id` and `run_hash_key`), packages
them into a `RunContext`, and signs the context using an attestation-bound
signing key.

## Integration in Trusted Shuffler Workflow

In the Trusted Shuffler architecture, the `run_context_initializer` stage is
executed **first** by the pipeline orchestrator before any other processing
stages.

The orchestrator integrates this stage into its overall workflow as follows:

1.  **Pipeline Initiation & Configuration**: The orchestrator constructs a
    `RunConfig` defining the run parameters (such as metric identifiers, epoch
    range, and target batch size).

2.  **Bootstrapping Stage Execution**: The orchestrator invokes the
    `run_context_initializer` container as the initial pipeline step, submitting
    the `RunConfig` as `unencrypted_data` via a `WriteRequest`.

3.  **Security Context Generation**: The container validates the `RunConfig`,
    generates 32-byte cryptographically secure random values for `run_id` and
    `run_hash_key`, constructs the `RunContext`, and signs
    `serialized_run_context` using its attestation-bound
    `InstanceSigningKeyHandle`. It emits the resulting `SignedRunContext`
    encrypted such that it can be read by further steps in the pipeline.

4.  **Context Propagation to Downstream Stages**: The orchestrator collects the
    `SignedRunContext` output and provides it as a side-input / configuration to
    all subsequent pipeline stages (e.g., Encryption Key Generation, Mapper,
    Shuffler, Reducer).

5.  **Cryptographic Boundary & Verification**: Downstream confidential
    containers verify the signature of `SignedRunContext` against the
    initialization stage's attestation public key. This guarantees that all
    downstream stages operate within the same cryptographic boundary, share
    identical run identifiers (`run_id`, `run_hash_key`), and adhere to the
    validated `RunConfig` without risk of parameter tampering or run confusion.

## Inputs & Outputs

### Inputs

#### Data Payload via `WriteRequest`

-   **`unencrypted_data`**: A serialized `RunConfig` protocol buffer containing
    configuration parameters including `metric_identifiers`, `min_epoch_index`,
    `max_epoch_index`, and `target_batch_size`.

--------------------------------------------------------------------------------

### Outputs

#### Encrypted `SignedRunContext`

-   **Key Index**: 0 (`kReencryptionKeyIndex`)
-   **Value**: A serialized `SignedRunContext` protocol buffer, containing:
    -   `serialized_run_context`: A serialized `RunContext` message containing:
    -   `run_id`: A 32-byte cryptographically secure random run ID.
    -   `run_hash_key`: A 32-byte cryptographically secure random hashing key.
    -   `run_config`: The validated input `RunConfig`.
    -   `signature`: The cryptographic signature of `serialized_run_context`
        generated using the container's attestation signing key.
