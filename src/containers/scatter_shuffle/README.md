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

# Scatter Shuffle Container

The `scatter_shuffle` container is the confidential data transformation stage responsible for the Scatter Shuffle and Group Assignment phase of the Trusted Shuffler two-pass shuffle.

## Overview

This container implements the `DoFn` / `FnConfidentialTransform` API for scatter shuffle and group assignment per [b/557296992](https://b.corp.google.com/issues/557296992).

In the two-pass shuffling architecture, the scatter shuffle container processes batches of messages partitioned by `MetricEpochKey`. It validates that all messages share an identical `MetricEpochKey`, `run_id`, and `shuffle_count == 0`, and confirms that the batch contains exactly `actual_batch_size` messages (including dummy padding messages from the pre-padding stage). It cryptographically shuffles the batch, assigns each message a `group_id = new_position % batch_num`, increments `shuffle_count` to 1, and re-encrypts the message payload.

**Note:** This container currently serves as an empty skeleton. Its `DoFn` is a no-op placeholder that returns OK status without processing inputs.
TODO(b/557296992): Implement message decryption, batch validation, cryptographic shuffling, group assignment, and payload re-encryption logic.

---

## Inputs & Outputs

### Inputs
- **`SignedRunContext`**: Verified via `VerifySignedRunContext` or initialized during replica configuration.
- **Batch Messages**: `actual_batch_size` messages of `EncryptedKV<MetricEpochKey, ShuffledMessage, 6>`:
  - Real customer messages encrypted under decryption key 6 (`is_padding == false`).
  - Unencrypted dummy padding messages from the pre-padding stage (`is_padding == true`).

### Outputs
- **Streaming Output**: A stream of `EncryptedKV<int32 group_id, ShuffledMessage, 7>`:
  - Header: Visible unencrypted `group_id` (`new_position % batch_num`) used by the orchestrator for group routing (`GroupByGroupId`).
  - Payload: Re-encrypted `ShuffledMessage` under re-encryption key 7 with `shuffle_count = 1`.

---

## Configuration & Key Indices

- **Decryption Key 6**: Used to decrypt incoming real `ShuffledMessage` payloads.
- **Re-encryption Key 7** (`dst_nodes` index 0): Used to encrypt output `ShuffledMessage` payloads forwarded to downstream group routing and merge stages.
