---
trigger: model_decision
description: "Rules, design patterns, and testing conventions for C++ container and transform development in trusted-shuffler."
---

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

# C++ & Container Design Conventions

This document specifies repository-wide C++ standards and testing patterns for confidential containers and transforms in the `trusted-shuffler` codebase. All container implementations and test suites must adhere to these principles.

---

## 1. Lifecycle Integrity over Testing Shortcuts

### Rule
Components that implement lifecycle initialization contracts (such as `confidential_federated_compute::fns::DoFn::InitializeReplica` or `FinalizeReplica`) must receive session configuration strictly through those lifecycle hooks. **Never add testing-only backdoor constructors or state-injection setters.**

DoFn instances must be constructed only via parameters provided by the corresponding `FnFactory`. If a container requires session configuration (e.g. `SignedRunContext`, configuration protos), that configuration must be unpacked from `google::protobuf::Any` inside `InitializeReplica`.

### Anti-Pattern (DO NOT DO)
```cpp
// BAD: Adding a testing-only constructor to inject state directly into DoFn.
class IngestionVerificationFn : public DoFn {
 public:
  // Anti-pattern: Bypasses InitializeReplica during unit tests.
  IngestionVerificationFn(SignedRunContext signed_run_context);
};
```

### Preferred Pattern (DO)
```cpp
// GOOD: Construct through factory or test InitializeReplica explicitly.
TEST(IngestionVerificationFnTest, InitializesFromSignedRunContext) {
  IngestionVerificationFn fn(constraints);
  FakeContext context;
  SignedRunContext signed_context = CreateValidSignedRunContext();
  google::protobuf::Any config;
  config.PackFrom(signed_context);

  ASSERT_OK(fn.InitializeReplica(config, context));
  EXPECT_TRUE(fn.SignedRunContext().has_value());
}
```

---

## 2. Parameter Explicitness & Absence of Hidden Defaults

### Rule
Never provide default parameter values (`= std::nullopt`, `= true`, `= false`, `= 0`) in constructors, factory methods, or public API entry points for mandatory session state or configuration parameters.

If a configuration parameter or flag is required for correct execution or security guarantees, callers and test fixtures must supply it explicitly.

### Anti-Pattern (DO NOT DO)
```cpp
// BAD: Defaulting a mandatory parameter hides missing configuration.
class IngestionVerificationFn : public DoFn {
 public:
  explicit IngestionVerificationFn(
      TrustedShufflerConfigConstraints constraints,
      std::optional<SignedRunContext> signed_run_context = std::nullopt);
};

// BAD: Defaulting thread compatibility on a pipeline stage.
class ReleaserStage {
 public:
  explicit ReleaserStage(std::string container_mpm, bool thread_compatible = true);
};
```

### Preferred Pattern (DO)
```cpp
// GOOD: Require callers to be explicit about required state.
class IngestionVerificationFn : public DoFn {
 public:
  explicit IngestionVerificationFn(
      TrustedShufflerConfigConstraints constraints);
};

class ReleaserStage {
 public:
  explicit ReleaserStage(std::string container_mpm, bool thread_compatible);
};
```

---

## 3. PascalCase for All Named Accessors / Getters in C++ Classes

### Rule
All named accessor and getter methods in C++ classes must use **PascalCase** (e.g. `SignedRunContext()`, `ConfigConstraints()`, `PublicKey()`, `SessionId()`, `MessageCount()`). **Never use `lower_snake_case` for class getters or accessors.**

### Anti-Pattern (DO NOT DO)
```cpp
// BAD: Using lower_snake_case for class getters.
const std::optional<SignedRunContext>& signed_run_context() const;
const TrustedShufflerConfigConstraints& config_constraints() const;
int32_t message_count() const;
bool is_ready() const;
```

### Preferred Pattern (DO)
```cpp
// GOOD: PascalCase for all named class accessors.
const std::optional<SignedRunContext>& SignedRunContext() const;
const TrustedShufflerConfigConstraints& ConfigConstraints() const;
int32_t MessageCount() const;
bool IsReady() const;
```

---

## 4. Declarative, Self-Describing Test Fixtures (No Magic Positional Tuples)

### Rule
Tests must be readable and self-describing without requiring the reader to cross-reference struct definitions or count positional arguments.
- **DO NOT** use raw positional numeric tuples (e.g. `{1001, 2, 50}`).
- **DO** use C++20 designated initializers (`{.project_id = 1001, ...}`) or clean helper structs in an anonymous namespace.
- Keep helper utilities in anonymous namespaces focused and general; avoid repeating proto-building boilerplate across tests.

### Anti-Pattern (DO NOT DO)
```cpp
// BAD: Opaque magic numbers whose fields cannot be identified at call site.
CreateConfigConstraints({{1001, 2, 50}, {1001, 3, 100}});
```

### Preferred Pattern (DO)
```cpp
// GOOD: Declarative, self-documenting test specifications.
namespace {

struct MetricThresholdSpec {
  int32_t project_id;
  int32_t metric_id;
  int64_t threshold;
};

TrustedShufflerConfigConstraints CreateConfigConstraints(
    absl::Span<const MetricThresholdSpec> specs) {
  TrustedShufflerConfigConstraints constraints;
  for (const auto& spec : specs) {
    auto* mt = constraints.add_metric_thresholds();
    mt->set_project_id(spec.project_id);
    mt->set_metric_id(spec.metric_id);
    mt->set_threshold(spec.threshold);
  }
  return constraints;
}

}  // namespace

TEST(IngestionVerificationTransformTest, IngestsValidConstraints) {
  auto constraints = CreateConfigConstraints({
      {.project_id = 1001, .metric_id = 2, .threshold = 50},
      {.project_id = 1001, .metric_id = 3, .threshold = 100},
  });
  // ...
}
```

---

## 5. Declarative Test Helpers & Lean Test Abstractions

### Rule: Write Simple Test Helpers for Declarative Setup
Write simple, focused helper abstractions and factory functions in an anonymous namespace to make unit tests declarative and clear. Encapsulate repetitive protobuf building, serialized configuration packaging, or fake context setup into helpers so test bodies read cleanly from top to bottom.

Always require all arguments explicitly in test helper functions—**do not use default parameters**.

### Preferred Pattern (DO)
```cpp
// GOOD: Small helpers in anonymous namespace make test bodies declarative and readable.
namespace {

SignedRunContext CreateTestSignedRunContext(absl::string_view payload) {
  SignedRunContext context;
  context.set_serialized_run_context(std::string(payload));
  context.set_signature("valid-signature");
  return context;
}

google::protobuf::Any PackToAny(const google::protobuf::Message& message) {
  google::protobuf::Any any;
  any.PackFrom(message);
  return any;
}

}  // namespace

TEST(IngestionVerificationFnTest, SucceedsWhenSignedRunContextProvided) {
  IngestionVerificationFn fn(CreateDefaultConstraints());
  FakeContext context;

  ASSERT_OK(fn.InitializeReplica(
      PackToAny(CreateTestSignedRunContext("valid-run-context-bytes")), context));
  EXPECT_OK(fn.Do(MakeRecord("key", "value"), context));
}
```

### Rule: Avoid Redundant or Obscuring Test Wrappers
Keep test abstractions lean and focused:
- Do not write wrapper functions in test files that are only called by a single test case or exist solely to wrap an assertion.
- Prefer standard GoogleMock matchers (`ElementsAre`, `AllOf`, `Property`, `StatusIs`) or shared repository matchers (`//src/testing:matchers`).

### Anti-Pattern (DO NOT DO)
```cpp
// BAD: Single-use wrapper that obscures the test setup and assertions.
absl::StatusOr<std::unique_ptr<FnFactory>> ProvideInvalidFactoryOnlyForTest() {
  return ProvideIngestionVerificationFnFactory(Any(), Any(), Any());
}
```
