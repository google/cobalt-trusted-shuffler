# Message Counter Container

The `message_counter` container is a confidential data transform that counts incoming messages while forwarding them, eventually producing a final summary report of the total count.

## Overview

This container implements the `ConfidentialTransformBase` API to process incoming message streams. It performs two primary functions:
1. **Message Forwarding**: Pass incoming messages through, emitting them in encrypted form.
2. **Aggregation**: Maintain a running count of the total number of messages processed during the session, and output a final unencrypted report containing this count.

---

## Configuration

The container must be configured before processing data. This is done by sending a `ConfigureRequest` containing a `CounterConfig` protocol buffer.

### The `CounterConfig` Proto
- **`counter_name`** (`string`): A name to identify this specific counter. This name will be included in the final output report.

---

## Inputs & Outputs

### Inputs

#### 1. Data Stream via `WriteRequest`
The container receives a sequence of data payloads through the `Write` method:
- **`unencrypted_data`** (`string`): The raw message payload.

---

### Outputs

The container produces outputs during both the streaming and finalization phases. All outputs are emitted as Key-Value (`KV`) pairs, where the key is a serialized `OutputKey` proto that describes the type of the output.

#### The `OutputKey` Proto
- **`type`** (`enum OutputType`):
  - `MESSAGE` (2): Represents an individual forwarded message.
  - `REPORT` (1): Represents the final aggregation report.

#### 1. Forwarded Messages (Emitted during `Write`)
For every message written to the container, it immediately emits:
- **Key**: An `OutputKey` with `type = MESSAGE`.
- **Value**: The original `unencrypted_data` payload, encrypted using the session's re-encryption key.

#### 2. Final Count Report (Emitted during `Finalize`)
When the session is finalized, the container emits a single unencrypted report:
- **Key**: An `OutputKey` with `type = REPORT`.
- **Value**: A serialized `CountReport` protocol buffer.

### The `CountReport` Proto
- **`counter_name`** (`string`): The name specified during configuration.
- **`count`** (`int64`): The total number of messages received and processed during the session.
