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

#include "src/containers/base/integrity_fault.h"

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/containers/base/counter_names.h"
#include "src/containers/base/with_outputs.h"
#include "src/proto/common.pb.h"
#include "src/proto/release.pb.h"
#include "src/proto/scatter_shuffle.pb.h"
#include "src/testing/fake_context.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;

class TestDefaultContainer
    : public TrustedDoFn::WithInputs<>::WithOutputs<EncryptsTo<>,
                                                    UnencryptedKV<IntegrityFault, IntegrityFault>> {
 public:
  absl::Status Do(KV input, DoContext& context) override { return absl::OkStatus(); }
};

class TestReleaseContainer : public TrustedDoFn::WithInputs<>::WithOutputs<
                                 EncryptsTo<>, UnencryptedKV<ReleaseOutputHeader, IntegrityFault>> {
 public:
  absl::Status Do(KV input, DoContext& context) override { return absl::OkStatus(); }
};

class TestScatterContainer
    : public TrustedDoFn::WithInputs<>::WithOutputs<
          EncryptsTo<>, UnencryptedKV<ScatterShuffleHeader, IntegrityFault>> {
 public:
  absl::Status Do(KV input, DoContext& context) override { return absl::OkStatus(); }
};

TEST(IntegrityFaultHandlerTest, InitialStateHasNoFault) {
  IntegrityFaultHandler<> handler;
  EXPECT_FALSE(handler.HasFault());
  EXPECT_FALSE(handler.FaultEmitted());
}

TEST(IntegrityFaultErrorTest, ToStatusTranslatesCorrectly) {
  IntegrityFaultError none(IntegrityFaultError::Error::kNone, "IntegrityFault: Custom");
  EXPECT_THAT(none.ToStatus(),
              StatusIs(absl::StatusCode::kFailedPrecondition, "IntegrityFault: Custom"));
  EXPECT_TRUE(none.ok());
  EXPECT_EQ(none.error(), IntegrityFaultError::Error::kNone);
  EXPECT_EQ(none.message(), "IntegrityFault: Custom");

  IntegrityFaultError already(IntegrityFaultError::Error::kAlreadyEmitted,
                              "Integrity fault already emitted.");
  EXPECT_THAT(already.ToStatus(),
              StatusIs(absl::StatusCode::kFailedPrecondition, "Integrity fault already emitted."));
  EXPECT_FALSE(already.ok());
  EXPECT_EQ(already.error(), IntegrityFaultError::Error::kAlreadyEmitted);

  IntegrityFaultError failed_emit(IntegrityFaultError::Error::kFailedEmission, "Channel closed");
  EXPECT_THAT(failed_emit.ToStatus(), StatusIs(absl::StatusCode::kInternal, "Channel closed"));
  EXPECT_FALSE(failed_emit.ok());
  EXPECT_EQ(failed_emit.error(), IntegrityFaultError::Error::kFailedEmission);
}

TEST(IntegrityFaultHandlerTest, SignalFaultDefaultEmitsIntegrityFault) {
  testing::FakeCombinedContext context;
  TestDefaultContainer container;
  IntegrityFaultHandler<> handler;

  IntegrityFaultError error = handler.SignalFault(container, context);

  EXPECT_EQ(error.error(), IntegrityFaultError::Error::kNone);
  EXPECT_EQ(error.message(), "IntegrityFault");
  EXPECT_THAT(error.ToStatus(), StatusIs(absl::StatusCode::kFailedPrecondition, "IntegrityFault"));
  EXPECT_TRUE(handler.HasFault());
  EXPECT_TRUE(handler.FaultEmitted());

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kUnencryptedOutputRecordCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kIntegrityFaultCount)), Eq(1));
  IntegrityFault emitted_header;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&emitted_header));

  IntegrityFault fault_payload;
  ASSERT_TRUE(fault_payload.ParseFromString(context.Unencrypted()[0].data));

  EXPECT_THAT(context.Errors(), IsEmpty());
}

TEST(IntegrityFaultHandlerTest, SignalFaultWithReasonFormatsAndEmits) {
  testing::FakeCombinedContext context;
  TestDefaultContainer container;
  IntegrityFaultHandler<> handler;

  IntegrityFaultError error = handler.SignalFault("Run ID mismatch", container, context);

  EXPECT_EQ(error.error(), IntegrityFaultError::Error::kNone);
  EXPECT_EQ(error.message(), "IntegrityFault: Run ID mismatch");
  EXPECT_THAT(error.ToStatus(),
              StatusIs(absl::StatusCode::kFailedPrecondition, "IntegrityFault: Run ID mismatch"));
  EXPECT_TRUE(handler.HasFault());
  EXPECT_TRUE(handler.FaultEmitted());

  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kUnencryptedOutputRecordCount)), Eq(1));
  EXPECT_THAT(context.GetCounters().at(std::string(kIntegrityFaultCount)), Eq(1));
  EXPECT_THAT(context.Errors(), IsEmpty());
}

TEST(IntegrityFaultHandlerTest, SignalFaultWorksWithConstContainer) {
  testing::FakeCombinedContext context;
  const TestDefaultContainer const_container;
  IntegrityFaultHandler<> handler;

  IntegrityFaultError error =
      handler.SignalFault("Const container fault", const_container, context);

  EXPECT_EQ(error.error(), IntegrityFaultError::Error::kNone);
  EXPECT_THAT(error.ToStatus(), StatusIs(absl::StatusCode::kFailedPrecondition,
                                         "IntegrityFault: Const container fault"));
  EXPECT_TRUE(handler.HasFault());
  EXPECT_TRUE(handler.FaultEmitted());
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));
}

TEST(IntegrityFaultHandlerTest, SignalFaultWithCustomHeaderProtoConstructor) {
  testing::FakeCombinedContext context;
  TestReleaseContainer container;
  ReleaseOutputHeader header;
  header.set_type(ReleaseOutputHeader::TYPE_INTEGRITY_FAULT);
  IntegrityFaultHandler<ReleaseOutputHeader> handler(header);

  IntegrityFaultError error = handler.SignalFault("Custom error", container, context);

  EXPECT_EQ(error.error(), IntegrityFaultError::Error::kNone);
  EXPECT_THAT(error.ToStatus(),
              StatusIs(absl::StatusCode::kFailedPrecondition, "IntegrityFault: Custom error"));
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));

  ReleaseOutputHeader emitted_header;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&emitted_header));
  EXPECT_EQ(emitted_header.type(), ReleaseOutputHeader::TYPE_INTEGRITY_FAULT);

  EXPECT_THAT(context.Errors(), IsEmpty());
}

TEST(IntegrityFaultHandlerTest, SignalFaultWithScatterShuffleHeaderConstructor) {
  testing::FakeCombinedContext context;
  TestScatterContainer container;
  ScatterShuffleHeader header;
  header.mutable_integrity_fault();

  IntegrityFaultHandler<ScatterShuffleHeader> handler(header);

  IntegrityFaultError error = handler.SignalFault("Shuffle failure", container, context);

  EXPECT_EQ(error.error(), IntegrityFaultError::Error::kNone);
  EXPECT_THAT(error.ToStatus(),
              StatusIs(absl::StatusCode::kFailedPrecondition, "IntegrityFault: Shuffle failure"));
  ASSERT_THAT(context.Unencrypted(), SizeIs(1));

  ScatterShuffleHeader emitted_header;
  ASSERT_TRUE(context.Unencrypted()[0].key.UnpackTo(&emitted_header));
  EXPECT_TRUE(emitted_header.has_integrity_fault());
  EXPECT_THAT(context.Errors(), IsEmpty());
}

TEST(IntegrityFaultHandlerTest, SingleEmissionAcrossMultipleSignalFaultInvocations) {
  testing::FakeCombinedContext context;
  TestDefaultContainer container;
  IntegrityFaultHandler<> handler;

  IntegrityFaultError error1 = handler.SignalFault("First error", container, context);
  EXPECT_EQ(error1.error(), IntegrityFaultError::Error::kNone);
  EXPECT_THAT(error1.ToStatus(),
              StatusIs(absl::StatusCode::kFailedPrecondition, "IntegrityFault: First error"));
  EXPECT_TRUE(handler.HasFault());
  EXPECT_TRUE(handler.FaultEmitted());
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));
  EXPECT_THAT(context.Errors(), IsEmpty());

  // Second invocation returns IntegrityFaultError with Error::kAlreadyEmitted without re-emitting.
  IntegrityFaultError error2 = handler.SignalFault(container, context);
  EXPECT_EQ(error2.error(), IntegrityFaultError::Error::kAlreadyEmitted);
  EXPECT_THAT(error2.ToStatus(),
              StatusIs(absl::StatusCode::kFailedPrecondition, "Integrity fault already emitted."));
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));
  EXPECT_THAT(context.Errors(), IsEmpty());

  // Third invocation with a new reason also returns Error::kAlreadyEmitted without re-emitting.
  IntegrityFaultError error3 = handler.SignalFault("Third error", container, context);
  EXPECT_EQ(error3.error(), IntegrityFaultError::Error::kAlreadyEmitted);
  EXPECT_THAT(error3.ToStatus(),
              StatusIs(absl::StatusCode::kFailedPrecondition, "Integrity fault already emitted."));
  EXPECT_THAT(context.Unencrypted(), SizeIs(1));
  EXPECT_THAT(context.Errors(), IsEmpty());
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
