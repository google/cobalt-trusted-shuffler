# TensorFlow Federated (TFF) Aggregation Protocols

This directory contains build patches and configuration for Google Parfait's `tensorflow-federated` fork (`org_tensorflow_federated`).

## Background & Architecture

The Trusted Shuffler ingestion stage parses client upload payloads formatted as Federated Compute wire format checkpoints ("FCKP"). In the Confidential Federated Compute (CFC) ecosystem, checkpoint serialization and deserialization are handled by TFF's aggregation core library (`tensorflow_federated/cc/core/impl/aggregation/protocol:*`).

### Why `tff.patch` is Required

1. **Bzlmod Compatibility**: Upstream TFF repository rules rely on legacy Bazel WORKSPACE definitions that reference `@com_google_absl`. `tff.patch` updates include paths and build targets to resolve `@abseil-cpp` under modern Bzlmod module resolution.
2. **Stripping Heavy Dependencies**: TFF includes extensive Python runtimes, ML models, and full TensorFlow targets. The patch isolates the lightweight C++ aggregation protocol parser (`FederatedComputeCheckpointParserFactory` and `FederatedComputeCheckpointBuilderFactory`) and eliminates unused transitive build targets.

### Stubbing `org_tensorflow`

Confidential containers execute in minimal distroless Debian 12 Trusted Execution Environments (TEEs). Pulling in full TensorFlow C++ binaries would bloat the container bundle by hundreds of megabytes and introduce unnecessary attack surface.

Instead, `@confidential-federated-compute` provides stub build rules (via `@confidential-federated-compute//:stub_tf_proto_library.bzl`) that stub out TensorFlow tensor protos and dependencies, allowing TFF's checkpoint parsing logic to compile natively without linking the TensorFlow runtime.
