// Copyright 2026 Google LLC.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "src/containers/base/with_outputs.h"

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "google/protobuf/any.pb.h"
#include "gtest/gtest.h"
#include "src/containers/base/pipeline_nodes.h"
#include "src/proto/common.pb.h"
#include "src/proto/ingestion.pb.h"
#include "src/proto/run_context.pb.h"
#include "src/testing/fake_context.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;

// A misdeclared container is rejected when it is compiled, which no runtime
// test can reach. Each rejection reads one of the predicates asserted here, and
// both polarities of each are pinned: widening a predicate fails an assertion
// here instead of silently admitting a container that emits to the wrong data
// node.
namespace compile_time_contract {

// The first template argument of WithOutputs is the list of data nodes the
// container encrypts to.
static_assert(internal::kIsEncryptsTo<EncryptsTo<>>);
static_assert(internal::kIsEncryptsTo<EncryptsTo<Node::kRunContext>>);
static_assert(!internal::kIsEncryptsTo<Encrypted<SignedRunContext, Node::kRunContext>>);

// The remaining template arguments are output streams.
static_assert(internal::kIsOutputStream<Encrypted<SignedRunContext, Node::kRunContext>>);
static_assert(internal::kIsOutputStream<UnencryptedKV<MetricEpochKey, IntegrityFault>>);
static_assert(!internal::kIsOutputStream<EncryptsTo<Node::kRunContext>>);

// Node::kNone is not a data node and cannot be read from or encrypted to.
static_assert(internal::kAreDataNodes<Node::kClientUploads>);
static_assert(internal::kAreDataNodes<Node::kRunContext>);
static_assert(!internal::kAreDataNodes<Node::kNone>);
static_assert(!internal::kAreDataNodes<Node::kRunContext, Node::kNone>);

// Data nodes are listed once each and in ascending order.
static_assert(internal::kAreAscending<>);
static_assert(internal::kAreAscending<Node::kPartialIngestionReports>);
static_assert(internal::kAreAscending<Node::kClientUploads, Node::kRunContext>);
static_assert(internal::kAreAscending<Node::kIngestedMessages, Node::kPartialIngestionReports>);
static_assert(!internal::kAreAscending<Node::kPartialIngestionReports, Node::kIngestedMessages>);
static_assert(!internal::kAreAscending<Node::kIngestedMessages, Node::kIngestedMessages>);

// WithInputs exposes the declared input sources.
static_assert(WithInputs<>::kSources.empty());
static_assert(WithInputs<Node::kClientUploads, Node::kRunContext>::kSources.size() == 2);
static_assert(WithInputs<Node::kClientUploads, Node::kRunContext>::kSources[0] ==
              Node::kClientUploads);
static_assert(WithInputs<Node::kClientUploads, Node::kRunContext>::kSources[1] ==
              Node::kRunContext);

// An encrypted stream names the data node it is encrypted to; an unencrypted
// one names none.
static_assert(internal::kDestinationMatchesVisibility<Visibility::kEncrypted, Node::kRunContext>);
static_assert(internal::kDestinationMatchesVisibility<Visibility::kUnencrypted, Node::kNone>);
static_assert(!internal::kDestinationMatchesVisibility<Visibility::kEncrypted, Node::kNone>);
static_assert(
    !internal::kDestinationMatchesVisibility<Visibility::kUnencrypted, Node::kRunContext>);

// Every data node a stream is encrypted to is one the container declared.
static_assert(internal::kStreamDestinationIsDeclared<
              EncryptsTo<Node::kRunContext>, Encrypted<SignedRunContext, Node::kRunContext>>);
static_assert(internal::kStreamDestinationIsDeclared<
              EncryptsTo<>, UnencryptedKV<MetricEpochKey, IntegrityFault>>);
static_assert(!internal::kStreamDestinationIsDeclared<
              EncryptsTo<Node::kRunContext>, Encrypted<SignedRunContext, Node::kIngestedMessages>>);

// A data node's position in the declared list is its re-encryption key index.
static_assert(EncryptsTo<Node::kIngestedMessages,
                         Node::kPartialIngestionReports>::IndexOf<Node::kIngestedMessages>() == 0);
static_assert(EncryptsTo<Node::kIngestedMessages, Node::kPartialIngestionReports>::IndexOf<
                  Node::kPartialIngestionReports>() == 1);
static_assert(EncryptsTo<Node::kIngestedMessages,
                         Node::kPartialIngestionReports>::IndexOf<Node::kRunContext>() ==
              kNotADestination);
static_assert(EncryptsTo<>::IndexOf<Node::kRunContext>() == kNotADestination);

// Streams are selected on visibility and value type together, so the same value
// type on an encrypted and an unencrypted stream resolves to two declarations.
static_assert(internal::FindOutputStream<Visibility::kUnencrypted, IntegrityFault,
                                         Encrypted<IntegrityFault, Node::kRunContext>,
                                         Unencrypted<IntegrityFault>>() == 1);
static_assert(internal::FindOutputStream<Visibility::kEncrypted, IntegrityFault,
                                         Encrypted<IntegrityFault, Node::kRunContext>,
                                         Unencrypted<IntegrityFault>>() == 0);

// A value type with no declared stream of that visibility does not resolve.
static_assert(internal::FindOutputStream<Visibility::kUnencrypted, ShuffledMessage,
                                         Encrypted<ShuffledMessage, Node::kRunContext>>() ==
              internal::kNoMatchingStream);

// Two streams sharing a (visibility, value type) pair are ambiguous and are
// rejected rather than silently resolving to the first one.
static_assert(internal::FindOutputStream<Visibility::kEncrypted, ShuffledMessage,
                                         Encrypted<ShuffledMessage, Node::kRunContext>,
                                         Encrypted<ShuffledMessage, Node::kIngestedMessages>>() ==
              internal::kNoMatchingStream);
static_assert(internal::kStreamHasUniqueVisibilityAndValue<
              Encrypted<ShuffledMessage, Node::kRunContext>,
              Encrypted<ShuffledMessage, Node::kRunContext>, Unencrypted<ShuffledMessage>>);
static_assert(!internal::kStreamHasUniqueVisibilityAndValue<
              Encrypted<ShuffledMessage, Node::kRunContext>,
              Encrypted<ShuffledMessage, Node::kRunContext>,
              Encrypted<ShuffledMessage, Node::kIngestedMessages>>);

}  // namespace compile_time_contract

class FailingContext : public testing::FakeContext {
 public:
  bool EmitEncrypted(int reencryption_key_index,
                     confidential_federated_compute::Session::KV kv) override {
    return false;
  }

  bool EmitUnencrypted(confidential_federated_compute::Session::KV kv) override { return false; }
};

class SingleEncryptedDoFn
    : public TrustedDoFn::WithInputs<>::WithOutputs<
          EncryptsTo<Node::kRunContext>, Encrypted<SignedRunContext, Node::kRunContext>> {
 public:
  absl::Status Do(KV input, DoContext& context) override {
    SignedRunContext msg;
    msg.set_signature("sig");
    return EmitEncrypted(context, msg);
  }
};

// Declares its two destination nodes in the order the access policy lists them,
// so kIngestedMessages is key index 0 and kPartialIngestionReports is 1.
class MultiOutputDoFn
    : public TrustedDoFn::WithInputs<Node::kClientUploads, Node::kRunContext>::WithOutputs<
          EncryptsTo<Node::kIngestedMessages, Node::kPartialIngestionReports>,
          EncryptedKV<MetricEpochKey, ShuffledMessage, Node::kIngestedMessages>,
          EncryptedKV<MetricEpochKey, PartialIngestionReport, Node::kPartialIngestionReports>,
          UnencryptedKV<MetricEpochKey, IntegrityFault>> {
 public:
  absl::Status Do(KV input, DoContext& context) override { return absl::OkStatus(); }

  absl::Status EmitMessage(FnContext& context, const MetricEpochKey& key,
                           const ShuffledMessage& msg) {
    return EmitEncrypted(context, key, msg);
  }

  absl::Status EmitReport(FnContext& context, const MetricEpochKey& key,
                          const PartialIngestionReport& report) {
    return EmitEncrypted(context, key, report);
  }

  absl::Status EmitFault(FnContext& context, const MetricEpochKey& key,
                         const IntegrityFault& fault) {
    return EmitUnencrypted(context, key, fault);
  }
};

// Encrypts to a node that is not the first one it was given a key for, so its
// only encrypted stream emits under key index 1. Its first node is one it never
// emits to, which still occupies key index 0.
class SecondDestinationDoFn
    : public TrustedDoFn::WithInputs<Node::kRunContext>::WithOutputs<
          EncryptsTo<Node::kIngestedMessages, Node::kPartialIngestionReports>,
          Encrypted<PartialIngestionReport, Node::kPartialIngestionReports>> {
 public:
  absl::Status Do(KV input, DoContext& context) override {
    PartialIngestionReport report;
    report.mutable_partial_fingerprint()->set_run_id("run_1");
    return EmitEncrypted(context, report);
  }
};

// Sends two value types to a single data node, which they share a re-encryption
// key for.
class SharedDestinationDoFn
    : public TrustedDoFn::WithInputs<Node::kRunContext>::WithOutputs<
          EncryptsTo<Node::kIngestedMessages>, Encrypted<ShuffledMessage, Node::kIngestedMessages>,
          Encrypted<PartialIngestionReport, Node::kIngestedMessages>> {
 public:
  absl::Status Do(KV input, DoContext& context) override {
    if (absl::Status status = EmitEncrypted(context, ShuffledMessage()); !status.ok()) {
      return status;
    }
    return EmitEncrypted(context, PartialIngestionReport());
  }
};

// Emits no encrypted data, so it is given no re-encryption keys.
class NoEncryptedOutputDoFn : public TrustedDoFn::WithInputs<Node::kRunContext>::WithOutputs<
                                  EncryptsTo<>, UnencryptedKV<MetricEpochKey, IntegrityFault>> {
 public:
  absl::Status Do(KV input, DoContext& context) override {
    return EmitUnencrypted(context, MetricEpochKey(), IntegrityFault());
  }
};

// Declares an Any routing key, which is forwarded to KV.key unchanged, and
// encrypts to the second of the two nodes it holds keys for.
class AnyKeyDoFn
    : public TrustedDoFn::WithInputs<Node::kRunContext>::WithOutputs<
          EncryptsTo<Node::kRunContext, Node::kIngestedMessages>,
          EncryptedKV<google::protobuf::Any, SignedRunContext, Node::kIngestedMessages>> {
 public:
  absl::Status Do(KV input, DoContext& context) override {
    SignedRunContext msg;
    msg.set_signature("sig");
    return EmitEncrypted(context, std::move(input.key), msg);
  }
};

// Forwards whole KV pairs rather than a declared message type.
class KvPassthroughDoFn
    : public TrustedDoFn::WithInputs<Node::kCountedMessages>::WithOutputs<
          EncryptsTo<>, Unencrypted<confidential_federated_compute::Session::KV>> {
 public:
  absl::Status Do(KV input, DoContext& context) override {
    return EmitUnencrypted(context, std::move(input));
  }
};

// Exercises the keyless streams, whose emit overloads take a value alone and
// leave KV.key empty. The encrypted stream uses the third destination node so
// that the key index (2) is shown to come from the declaration.
class KeylessOutputsDoFn
    : public TrustedDoFn::WithInputs<Node::kRunContext>::WithOutputs<
          EncryptsTo<Node::kRunContext, Node::kIngestedMessages, Node::kPartialIngestionReports>,
          Encrypted<SignedRunContext, Node::kPartialIngestionReports>,
          Unencrypted<MetricEpochKey>> {
 public:
  absl::Status Do(KV input, DoContext& context) override { return absl::OkStatus(); }

  absl::Status EmitRunContext(FnContext& context, const SignedRunContext& run_context) {
    return EmitEncrypted(context, run_context);
  }

  absl::Status EmitSummary(FnContext& context, const MetricEpochKey& report) {
    return EmitUnencrypted(context, report);
  }
};

TEST(WithOutputsTest, EmitsKeylessEncryptedValueWithEmptyKey) {
  testing::FakeCombinedContext context;
  SingleEncryptedDoFn fn;

  EXPECT_THAT(fn.Do(SingleEncryptedDoFn::KV("data"), context), IsOk());
  ASSERT_THAT(context.Encrypted(), SizeIs(1));
  EXPECT_THAT(context.Encrypted()[0].second.key.type_url(), IsEmpty());
  EXPECT_THAT(context.Encrypted()[0].second.key.value(), IsEmpty());

  SignedRunContext parsed;
  ASSERT_TRUE(parsed.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_THAT(parsed.signature(), Eq("sig"));
}

TEST(WithOutputsTest, DispatchesMultipleOutputsToCorrectKeyIndices) {
  testing::FakeCombinedContext context;
  MultiOutputDoFn fn;

  MetricEpochKey key;
  key.set_project_id(101);
  key.set_metric_id(202);
  key.set_epoch_index(10);

  ShuffledMessage msg;
  msg.set_customer_message("payload");

  PartialIngestionReport report;
  report.mutable_partial_fingerprint()->set_run_id("run_1");

  IntegrityFault fault;

  EXPECT_THAT(fn.EmitMessage(context, key, msg), IsOk());
  EXPECT_THAT(fn.EmitReport(context, key, report), IsOk());
  EXPECT_THAT(fn.EmitFault(context, key, fault), IsOk());

  ASSERT_THAT(context.Encrypted(), SizeIs(2));
  EXPECT_THAT(context.Encrypted()[0].first, Eq(0));
  EXPECT_THAT(context.Encrypted()[1].first, Eq(1));

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  MetricEpochKey unpacked_fault_key;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&unpacked_fault_key));
  EXPECT_THAT(unpacked_fault_key.project_id(), Eq(101));
}

TEST(WithOutputsTest, PropagatesInternalErrorWhenContextEmitFails) {
  FailingContext failing_context;
  testing::FakeDoContext do_context(failing_context);
  SingleEncryptedDoFn fn;

  EXPECT_THAT(fn.Do(SingleEncryptedDoFn::KV("data"), do_context),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(WithOutputsTest, EncryptsToLaterDestinationUnderItsKeyIndex) {
  testing::FakeCombinedContext context;
  SecondDestinationDoFn fn;

  EXPECT_THAT(fn.Do(SecondDestinationDoFn::KV("data"), context), IsOk());
  ASSERT_THAT(context.Encrypted(), SizeIs(1));
  EXPECT_THAT(context.Encrypted()[0].first, Eq(1));

  PartialIngestionReport parsed;
  ASSERT_TRUE(parsed.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_THAT(parsed.partial_fingerprint().run_id(), Eq("run_1"));
}

TEST(WithOutputsTest, EncryptsStreamsSharingADestinationUnderOneKeyIndex) {
  testing::FakeCombinedContext context;
  SharedDestinationDoFn fn;

  EXPECT_THAT(fn.Do(SharedDestinationDoFn::KV("data"), context), IsOk());
  ASSERT_THAT(context.Encrypted(), SizeIs(2));
  EXPECT_THAT(context.Encrypted()[0].first, Eq(0));
  EXPECT_THAT(context.Encrypted()[1].first, Eq(0));
}

TEST(WithOutputsTest, EmitsUnencryptedWithoutAnyDestinations) {
  testing::FakeCombinedContext context;
  NoEncryptedOutputDoFn fn;

  EXPECT_THAT(fn.Do(NoEncryptedOutputDoFn::KV("data"), context), IsOk());
  EXPECT_THAT(context.Encrypted(), IsEmpty());
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));
}

TEST(WithOutputsTest, ForwardsAnyKeyWithoutNestingIt) {
  testing::FakeCombinedContext context;
  AnyKeyDoFn fn;

  MetricEpochKey routing_key;
  routing_key.set_project_id(101);
  google::protobuf::Any key_any;
  key_any.PackFrom(routing_key);

  EXPECT_THAT(fn.Do(AnyKeyDoFn::KV(key_any, "data"), context), IsOk());
  ASSERT_THAT(context.Encrypted(), SizeIs(1));
  EXPECT_THAT(context.Encrypted()[0].first, Eq(1));

  // The declared Any key is emitted as-is, not packed into a second Any.
  MetricEpochKey emitted_key;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&emitted_key));
  EXPECT_THAT(emitted_key.project_id(), Eq(101));
}

TEST(WithOutputsTest, ForwardsSessionKvPassthroughUnchanged) {
  testing::FakeCombinedContext context;
  KvPassthroughDoFn fn;

  google::protobuf::Any key_any;
  key_any.PackFrom(SignedRunContext());

  EXPECT_THAT(fn.Do(KvPassthroughDoFn::KV(key_any, "opaque_bytes", "blob-7"), context), IsOk());
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  EXPECT_THAT(context.Unencrypted()[0].key.type_url(), Eq(key_any.type_url()));
  EXPECT_THAT(context.Unencrypted()[0].data, Eq("opaque_bytes"));
  EXPECT_THAT(context.Unencrypted()[0].blob_id, Eq("blob-7"));
}

TEST(WithOutputsTest, PropagatesInternalErrorWhenUnencryptedPassthroughFails) {
  FailingContext failing_context;
  testing::FakeDoContext do_context(failing_context);
  KvPassthroughDoFn fn;

  EXPECT_THAT(fn.Do(KvPassthroughDoFn::KV("data"), do_context),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(WithOutputsTest, EmitsKeylessEncryptedValueAtDeclaredIndexWithEmptyKey) {
  testing::FakeCombinedContext context;
  KeylessOutputsDoFn fn;

  SignedRunContext run_context;
  run_context.set_signature("sig");

  ASSERT_THAT(fn.EmitRunContext(context, run_context), IsOk());
  ASSERT_THAT(context.Encrypted(), SizeIs(1));
  EXPECT_THAT(context.Encrypted()[0].first, Eq(2));
  EXPECT_THAT(context.Encrypted()[0].second.key.type_url(), IsEmpty());
  EXPECT_THAT(context.Encrypted()[0].second.key.value(), IsEmpty());

  SignedRunContext parsed;
  ASSERT_TRUE(parsed.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_THAT(parsed.signature(), Eq("sig"));
  EXPECT_THAT(context.Unencrypted(), IsEmpty());
}

TEST(WithOutputsTest, EmitsKeylessUnencryptedValueWithEmptyKey) {
  testing::FakeCombinedContext context;
  KeylessOutputsDoFn fn;

  MetricEpochKey report;
  report.set_project_id(101);
  report.set_epoch_index(7);

  ASSERT_THAT(fn.EmitSummary(context, report), IsOk());
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  EXPECT_THAT(context.Unencrypted()[0].key.type_url(), IsEmpty());
  EXPECT_THAT(context.Unencrypted()[0].key.value(), IsEmpty());

  MetricEpochKey parsed;
  ASSERT_TRUE(parsed.ParseFromString(context.Unencrypted()[0].data));
  EXPECT_THAT(parsed.project_id(), Eq(101));
  EXPECT_THAT(parsed.epoch_index(), Eq(7));
  EXPECT_THAT(context.Encrypted(), IsEmpty());
}

TEST(WithOutputsTest, PropagatesInternalErrorWhenKeylessUnencryptedEmitFails) {
  FailingContext failing_context;
  testing::FakeFnContext fn_context(failing_context);
  KeylessOutputsDoFn fn;

  EXPECT_THAT(fn.EmitSummary(fn_context, MetricEpochKey()), StatusIs(absl::StatusCode::kInternal));
}

TEST(WithOutputsTest, RoutesEachValueTypeToItsOwnStreamPayload) {
  testing::FakeCombinedContext context;
  MultiOutputDoFn fn;

  MetricEpochKey key;
  key.set_project_id(101);

  ShuffledMessage msg;
  msg.set_customer_message("payload");

  PartialIngestionReport report;
  report.mutable_partial_fingerprint()->set_run_id("run_1");

  ASSERT_THAT(fn.EmitMessage(context, key, msg), IsOk());
  ASSERT_THAT(fn.EmitReport(context, key, report), IsOk());
  ASSERT_THAT(context.Encrypted(), SizeIs(2));

  ShuffledMessage emitted_msg;
  ASSERT_TRUE(emitted_msg.ParseFromString(context.Encrypted()[0].second.data));
  EXPECT_THAT(emitted_msg.customer_message(), Eq("payload"));

  PartialIngestionReport emitted_report;
  ASSERT_TRUE(emitted_report.ParseFromString(context.Encrypted()[1].second.data));
  EXPECT_THAT(emitted_report.partial_fingerprint().run_id(), Eq("run_1"));

  MetricEpochKey emitted_key;
  ASSERT_TRUE(context.Encrypted()[0].second.key.UnpackTo(&emitted_key));
  EXPECT_THAT(emitted_key.project_id(), Eq(101));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
