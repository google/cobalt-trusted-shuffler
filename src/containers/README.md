# Proof of Concept Containers

The `message_counter` and `releaser` containers are used as part of a proof of concept pipeline.

## The `message_counter`

It is expected to be run first.

### Configuration

The Configuration request expects a CounterConfig message with an arbitrary string in the `counter_name` field.

### Inputs

This is the entry of the pipeline. It expects zero or more messages, encrypted with the `message_counter`'s reencryption key, to be written to the session as `WriteRequest`s.

### Outputs

Each input message is part of the output, encrypted and with a key set.
The key is `OutputKey` with the `OutputType` set to `MESSAGE`.

Additionally, there will be a `CountReport` as part of the output.
It is not encrypted and has a key set to `OutputKey` with the `OutputType` set to `REPORT`.
The `CountReport` should have the same `counter_name` as the one in the configuration request and the `count` should be equal to the number of input messages.

## The `releaser`

It is expected to be run after the `message_counter`.

### Configuration

None.

### Inputs

The input of the releaser is the outputs of the `messsage_counter` with an `OutputType` of `MESSAGE`.

### Outputs

Each input message is in the output, encrypted and marked for release.

In the `Finalize` step, the `release_token` is returned as part of the `FinalizeResponse` so that the message marked for release may be decrypted.