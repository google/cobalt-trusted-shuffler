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

# Agent Contribution Guide: Trusted Shuffler Containers

This document instructs AI agents contributing to the **Trusted Shuffler** repository. Follow these conventions, architectural patterns, privacy boundaries, and checklists when creating or modifying confidential containers.

---

## 1. System Overview & Architecture

Trusted Shuffler executes confidential data transformation pipelines inside Trusted Execution Environments (TEEs) using Google's **Confidential Federated Compute (CFC)** framework and **Oak Containers SDK**.

### Core Architecture Components

1. **Oak Container Runtime**: Runs distroless Debian 12 amd64 containers. Each container must run a gRPC server on `[::]:8080` and notify the Oak orchestrator when ready (`OrchestratorClient().NotifyAppReady()`).
2. **Fn / DoFn Framework (`@confidential-federated-compute//containers/fns`)**: The standard paradigm for container transforms:
   - `DoFn`: Implements data transformation logic across session lifecycles (`InitializeReplica`, `Do`, `FinalizeReplica`).
   - `FnFactory`: Instantiates `DoFn` instances for incoming client sessions.
   - `FnConfidentialTransform`: Exposes the gRPC transform server managing Oak signing and encryption key handles.

### Data Visibility & Privacy Boundary

Pipelines operate across a strict trust boundary between the **Untrusted Orchestrator** (host environment managing routing, batching, and scheduling) and **Confidential TEE Enclaves** (isolated container instances).

```
+-----------------------------------------------------------------------------------+
|                              Untrusted Orchestrator                               |
|   - Inspects all KV keys (routing metadata)                                       |
|   - Inspects plaintext values emitted via EmitUnencrypted                          |
|   - CANNOT read ciphertext from EmitEncrypted or EmitReleasable                    |
+----------------------------------------+------------------------------------------+
                                         |
                       KV Pairs (Encrypted / Unencrypted)
                                         |
                                         v
+-----------------------------------------------------------------------------------+
|                        Downstream Confidential TEE Stage                          |
|   - Holds session decryption key / release tokens                                 |
|   - Decrypts and processes opaque payloads inside hardware enclave                |
+-----------------------------------------------------------------------------------+
```

#### Prescriptive Rules for Data Emission:

1. **`context.EmitEncrypted(key_id, kv)`**:
   - **Visibility**: The `kv.key` is plaintext (visible to the untrusted orchestrator for routing). The `kv.value` is encrypted with the session's re-encryption key and is **completely opaque to the untrusted orchestrator**. Only downstream TEE containers holding the corresponding key can decrypt it.
   - **When to Use**: Streaming intermediate confidential payloads or forwarding transformed user messages to subsequent TEE stages.

2. **`context.EmitReleasable(key_id, kv, src_state, dst_state, release_token)`**:
   - **Visibility**: The `kv.key` is plaintext. The `kv.value` is encrypted and **opaque to the untrusted orchestrator** until session finalization conditions are satisfied and the `release_token` is revealed.
   - **When to Use**: Conditional or deferred release pipelines (e.g. threshold batching, k-anonymity gates, or final release containers like `releaser`).

3. **`context.EmitUnencrypted(kv)`**:
   - **Visibility**: **Both `kv.key` and `kv.value` are in plaintext and fully visible to the untrusted orchestrator**, host logging, and downstream consumers.
   - **When to Use**: Final aggregated summary reports (e.g. `CountReport`) or non-sensitive execution metrics.
   - **CRITICAL PRIVACY RULE**: Never emit raw client records, individual identifiers, or unaggregated confidential data with `EmitUnencrypted`.

---

## 2. Repository Layout

```
trusted-shuffler/
├── .agents/
│   └── AGENTS.md                  # Agent contribution instructions (this file)
├── build.sh                       # Local and CI build execution script
├── kokoro/rbe/                    # CI configurations (presubmit and continuous)
└── src/
    ├── containers/                # Container implementations
    ├── proto/                     # Protocol buffer schemas (shared and container-specific)
    └── testing/                   # Test utilities (FakeContext, FakeKeys, etc.)
```

---

## 3. Step-by-Step Guide to Adding a New Container

When creating a new container (e.g. `foo_bar`), you must complete **all** of the following steps:

### Step 0: Align with the User on Data Visibility & Privacy Boundary
Before writing code, clarify and align with the user on:
1. **Confidential vs. Public Data**: What payload data must remain opaque to the untrusted orchestrator and only readable by subsequent TEE stages (`EmitEncrypted` / `EmitReleasable`) vs. what data can be visible in plaintext (`EmitUnencrypted`).
2. **Routing Metadata**: What `OutputKey` structure or metadata must the untrusted orchestrator inspect to properly route records.
3. **Configuration Schema**: Whether the container is configured via container-specific configuration protos or shared pipeline context protos (such as `RunContext` / `SignedRunContext`).

### Step 1: Define Protos (`src/proto/`)
Define necessary schemas according to their scope:
- **Shared / Pipeline-wide Protos** (`src/proto/`): Put messages used across multiple stages or at the top-level pipeline (e.g. `run_context.proto`, `trusted_shuffler_upload.proto`) directly under `src/proto/` and register them in `src/proto/BUILD`.
- **Container-specific Protos** (`src/proto/foo_bar/`): Put schemas specific to this container in a dedicated subdirectory:
  - `foo_bar_config.proto`: Configuration proto unpacked during `InitializeReplica`.
  - `output_key.proto`: Key proto describing emitted KV pairs (e.g., `MESSAGE` vs `REPORT`).
  - `BUILD`: Define `proto_library` and `cc_proto_library` rules.

### Step 2: Implement the Container Logic (`src/containers/foo_bar/`)
1. **`foo_bar_fn.h` / `foo_bar_fn.cc`**:
   - Subclass `confidential_federated_compute::fns::DoFn`.
   - Implement `InitializeReplica(google::protobuf::Any config, Context& context)`.
   - Implement `Do(KV input, Context& context)` using the appropriate emission primitive (`EmitEncrypted`, `EmitReleasable`, or `EmitUnencrypted`).
   - Implement `FinalizeReplica(google::protobuf::Any config, Context& context)`.
2. **`foo_bar_transform.h` / `foo_bar_transform.cc`**:
   - Subclass `confidential_federated_compute::fns::FnFactory`.
   - Provide factory function:
     ```cpp
     std::unique_ptr<confidential_federated_compute::fns::FnConfidentialTransform>
     CreateFooBarTransform(
         std::unique_ptr<oak::crypto::SigningKeyHandle> signing_key_handle,
         std::unique_ptr<oak::crypto::EncryptionKeyHandle> encryption_key_handle);
     ```
3. **`main.cc`**:
   - Initialize gRPC server on `[::]:8080`.
   - Register the transform service.
   - Signal readiness to Oak orchestrator:
     ```cpp
     CHECK_OK(oak::containers::sdk::OrchestratorClient().NotifyAppReady());
     server->Wait();
     ```

### Step 3: Configure Bazel Packaging (`src/containers/foo_bar/BUILD`)
Every container package must define:
- `cc_library` targets for the `_fn` and `_transform`.
- `cc_binary(name = "main", ...)` with dependencies on `@oak//cc/containers/sdk:*` and `@grpc//:grpc++`.
- Container image & bundle rules:
  ```python
  load("@oak//bazel:defs.bzl", "oci_runtime_bundle")
  load("@rules_oci//oci:defs.bzl", "oci_image")
  load("@rules_pkg//pkg:tar.bzl", "pkg_tar")

  pkg_tar(
      name = "tar",
      srcs = [":main"],
  )

  oci_image(
      name = "oci_image",
      base = "@distroless_cc_debian12_linux_amd64",
      entrypoint = ["/main"],
      tars = [":tar"],
  )

  oci_runtime_bundle(
      name = "oci_runtime_bundle",
      image = ":oci_image",
  )
  ```

### Step 4: Write Unit and Transform Tests
- **`foo_bar_fn_test.cc`**: Use `//src/testing:fake_context` to verify data emission (`Unencrypted()`, `Encrypted()`, `Releasable()`, `Errors()`). Verify that sensitive fields are never emitted unencrypted.
- **`foo_bar_transform_test.cc`**: Use `//src/testing:fake_keys` and `//src/testing:testable_transform` to test gRPC session handling.

### Step 5: Document the Container
- Create `src/containers/foo_bar/README.md` documenting:
  - Container purpose & role in pipeline.
  - Configuration proto parameters.
  - Input stream expectations (format and schema of incoming KV pairs processed in `Do()`).
  - Output stream & finalize emit formats, explicitly documenting what is encrypted vs. plaintext.
- Update `src/containers/README.md` to reference the new container.

### Step 6: Update CI & Kokoro Build Pipeline (Required for Production Containers)
**IMPORTANT**: **WIP vs. Production Containers**: If you are creating a scaffolded, work-in-progress (WIP), or skeleton container (e.g. while developing stacked CLs), **DO NOT** add it to `build.sh` or Kokoro configs yet. Only wire Kokoro when the container is ready for CI builds and integration. Add `TODO(b/...)` comments following `go/todo-style` to document pending steps.

When ready for production release, failing to update Kokoro will cause builds and releases to miss the container artifact:
1. **`build.sh`**:
   Add artifact copy step:
   ```bash
   log "Copying foo_bar container output to kokoro artifacts dir."
   cp -Rp ./bazel-bin/src/containers/foo_bar/* ${KOKORO_ARTIFACTS_DIR}/foo_bar/.
   ```
2. **`kokoro/rbe/presubmit.cfg` & `kokoro/rbe/continuous.cfg`**:
   Add fileset artifact destination:
   ```protobuf
   fileset_artifacts {
     name: "foo_bar"
     artifact_globs: "foo_bar/oci_runtime_bundle.tar"
     strip_prefix: "foo_bar"
     error_if_missing: true
     destinations {
       placer {}
     }
   }
   ```

---

## 4. Development & Verification Commands

- **Run all tests**:
  ```bash
  bazel test //...
  ```
- **Build the container runtime bundle**:
  ```bash
  bazel build //src/containers/<name>:oci_runtime_bundle
  ```
- **Refresh compile commands**:
  ```bash
  bazel run //:refresh_compile_commands
  ```
- **Format code**:
  Apply `.clang-format` to all modified `.h` and `.cc` files.
- **Lockfile cleanliness**:
  Running local Bazel tests may touch `MODULE.bazel.lock`. If no dependencies were intentionally added, run `git restore MODULE.bazel.lock` before staging or committing.

---

## 5. C++ Architecture & Testing Standards

All C++ implementations, container transforms, and tests must adhere to the conventions documented in [`.agents/rules/cpp_conventions.md`](rules/cpp_conventions.md):
- **Lifecycle Integrity**: DoFn session configuration must be unpacked inside `InitializeReplica`; never add testing-only backdoor constructors or setters to `DoFn` classes.
- **Explicit Arguments**: Avoid default arguments (`= std::nullopt`, default booleans) in constructors or stage APIs where values must be passed explicitly.
- **Schema Casing**: Match Protobuf schema casing for accessor methods (`SignedRunContext()`, `ConfigConstraints()`).
- **Declarative Test Fixtures**: Use C++20 designated initializers (`{.project_id = 1, ...}`) and clean helper structs in anonymous namespaces instead of positional magic tuples.

---

## 6. Contribution Checklist for Agents

Before completing a task or submitting a change:
- [ ] Aligned with user on data visibility (orchestrator plaintext vs. downstream TEE ciphertext).
- [ ] Protos defined in `src/proto/<name>/` (or `src/proto/` for shared schemas) with `BUILD` rules.
- [ ] `DoFn` and transform service implemented in C++20 following [`.agents/rules/cpp_conventions.md`](rules/cpp_conventions.md).
- [ ] `main.cc` notifies `OrchestratorClient().NotifyAppReady()`.
- [ ] Container bundle (`oci_runtime_bundle`) defined in `BUILD`.
- [ ] Unit tests for `_fn` and `_transform` pass via `bazel test //...`.
- [ ] Tests verify that confidential data is emitted via `EmitEncrypted`/`EmitReleasable` and never `EmitUnencrypted`.
- [ ] Tests avoid backdoor constructors, using `InitializeReplica` and designated initializers.
- [ ] Container `README.md` and `src/containers/README.md` updated.
- [ ] If container is production-ready, `build.sh` and Kokoro configs updated; if WIP, kept excluded from Kokoro with `TODO(b/...)` comments.
- [ ] `git restore MODULE.bazel.lock` run if the lockfile was modified unintentionally.
- [ ] All code formatted with `.clang-format` and license header included.
