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

class FailingContext : public testing::FakeContext {
 public:
  bool EmitEncrypted(int reencryption_key_index,
                     confidential_federated_compute::Session::KV kv) override {
    return false;
  }

  bool EmitUnencrypted(confidential_federated_compute::Session::KV kv) override { return false; }
};

class SingleEncryptedDoFn : public TrustedDoFn::WithOutputs<Encrypted<SignedRunContext, 0>> {
 public:
  absl::Status Do(KV input, DoContext& context) override {
    SignedRunContext msg;
    msg.set_signature("sig");
    return EmitEncrypted(context, msg);
  }
};

class MultiOutputDoFn
    : public TrustedDoFn::WithOutputs<EncryptedKV<MetricEpochKey, ShuffledMessage, 0>,
                                      EncryptedKV<MetricEpochKey, PartialIngestionReport, 1>,
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

// Declares an Any routing key, which is forwarded to KV.key unchanged.
class AnyKeyDoFn
    : public TrustedDoFn::WithOutputs<EncryptedKV<google::protobuf::Any, SignedRunContext, 3>> {
 public:
  absl::Status Do(KV input, DoContext& context) override {
    SignedRunContext msg;
    msg.set_signature("sig");
    return EmitEncrypted(context, std::move(input.key), msg);
  }
};

// Forwards whole KV pairs rather than a declared message type.
class KvPassthroughDoFn
    : public TrustedDoFn::WithOutputs<Unencrypted<confidential_federated_compute::Session::KV>> {
 public:
  absl::Status Do(KV input, DoContext& context) override {
    return EmitUnencrypted(context, std::move(input));
  }
};

// Exercises the keyless streams, whose emit overloads take a value alone and
// leave KV.key empty. The encrypted stream uses a non-zero key index so that
// the index is shown to come from the declaration.
class KeylessOutputsDoFn
    : public TrustedDoFn::WithOutputs<Encrypted<SignedRunContext, 2>, Unencrypted<MetricEpochKey>> {
 public:
  absl::Status Do(KV input, DoContext& context) override { return absl::OkStatus(); }

  absl::Status EmitRunContext(FnContext& context, const SignedRunContext& run_context) {
    return EmitEncrypted(context, run_context);
  }

  absl::Status EmitSummary(FnContext& context, const MetricEpochKey& report) {
    return EmitUnencrypted(context, report);
  }
};

// Stream resolution is a compile-time function, so its contract is pinned with
// static_asserts rather than runtime expectations. The ambiguous and unmatched
// lists below are never used to instantiate WithOutputs, which would reject
// them outright.
namespace resolution {

using ::cobalt::trusted_shuffler::internal::FindOutputStream;
using ::cobalt::trusted_shuffler::internal::kNoMatchingStream;

// Visibility participates in selection: the same value type on an encrypted and
// an unencrypted stream resolves to two different declarations.
static_assert(FindOutputStream<Visibility::kUnencrypted, IntegrityFault,
                               Encrypted<IntegrityFault, 2>, Unencrypted<IntegrityFault>>() == 1);
static_assert(FindOutputStream<Visibility::kEncrypted, IntegrityFault, Encrypted<IntegrityFault, 2>,
                               Unencrypted<IntegrityFault>>() == 0);

// A value type with no declared stream of that visibility does not resolve.
static_assert(
    FindOutputStream<Visibility::kUnencrypted, ShuffledMessage, Encrypted<ShuffledMessage, 0>>() ==
    kNoMatchingStream);

// Two streams sharing a (visibility, value type) pair are ambiguous and are
// rejected rather than silently resolving to the first one.
static_assert(FindOutputStream<Visibility::kEncrypted, ShuffledMessage,
                               Encrypted<ShuffledMessage, 0>, Encrypted<ShuffledMessage, 1>>() ==
              kNoMatchingStream);

}  // namespace resolution

TEST(WithOutputsTest, EmitsEncryptedValueWithCorrectKeyIndex) {
  testing::FakeCombinedContext context;
  SingleEncryptedDoFn fn;

  EXPECT_THAT(fn.Do(SingleEncryptedDoFn::KV("data"), context), IsOk());
  ASSERT_THAT(context.Encrypted(), SizeIs(1));
  EXPECT_THAT(context.Encrypted()[0].first, Eq(0));

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

TEST(WithOutputsTest, ForwardsAnyKeyWithoutNestingIt) {
  testing::FakeCombinedContext context;
  AnyKeyDoFn fn;

  MetricEpochKey routing_key;
  routing_key.set_project_id(101);
  google::protobuf::Any key_any;
  key_any.PackFrom(routing_key);

  EXPECT_THAT(fn.Do(AnyKeyDoFn::KV(key_any, "data"), context), IsOk());
  ASSERT_THAT(context.Encrypted(), SizeIs(1));
  EXPECT_THAT(context.Encrypted()[0].first, Eq(3));

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
