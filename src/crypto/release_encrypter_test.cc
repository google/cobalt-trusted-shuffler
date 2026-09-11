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

#include "src/crypto/release_encrypter.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/proto/run_context.pb.h"
#include "tink/aead/aead_config.h"
#include "tink/aead/aead_key_templates.h"
#include "tink/aead/aes_gcm_key.h"
#include "tink/aead/aes_gcm_parameters.h"
#include "tink/config/global_registry.h"
#include "tink/insecure_secret_key_access.h"
#include "tink/key_status.h"
#include "tink/keyset_handle.h"
#include "tink/parameters.h"
#include "tink/partial_key_access.h"
#include "tink/proto_keyset_format.h"
#include "tink/proto_parameters_format.h"
#include "tink/restricted_data.h"
#include "tink/secret_data.h"
#include "tink/util/secret_data.h"

namespace cobalt::trusted_shuffler {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Ne;
using ::testing::Not;
using ::testing::SizeIs;

inline constexpr char kValid32ByteRawKey[] = "01234567890123456789012345678901";
inline constexpr std::size_t kAesGcmIvSize = 12;
inline constexpr std::size_t kAesGcmTagSize = 16;

absl::StatusOr<std::string> SerializeRawAesGcmKeyToKeyset(absl::string_view raw_key_bytes) {
  const absl::StatusOr<std::unique_ptr<crypto::tink::Parameters>> template_params =
      crypto::tink::ParseParametersFromProtoFormat(
          crypto::tink::AeadKeyTemplates::Aes256GcmNoPrefix().SerializeAsString());
  if (!template_params.ok()) {
    return template_params.status();
  }
  const auto* aes_gcm_params =
      dynamic_cast<const crypto::tink::AesGcmParameters*>(template_params->get());
  if (aes_gcm_params == nullptr) {
    return absl::InternalError("Expected AesGcmParameters from Aes256GcmNoPrefix template");
  }

  absl::StatusOr<crypto::tink::AesGcmKey> key = crypto::tink::AesGcmKey::Create(
      *aes_gcm_params,
      crypto::tink::RestrictedData(raw_key_bytes, crypto::tink::InsecureSecretKeyAccess::Get()),
      /*id_requirement=*/std::nullopt, crypto::tink::GetPartialKeyAccess());
  if (!key.ok()) {
    return key.status();
  }

  absl::StatusOr<crypto::tink::KeysetHandle> keyset_handle =
      crypto::tink::KeysetHandleBuilder()
          .AddEntry(crypto::tink::KeysetHandleBuilder::Entry::CreateFromKey(
              std::make_shared<const crypto::tink::AesGcmKey>(std::move(*key)),
              crypto::tink::KeyStatus::kEnabled, /*is_primary=*/true))
          .Build();
  if (!keyset_handle.ok()) {
    return keyset_handle.status();
  }

  absl::StatusOr<crypto::tink::SecretData> serialized =
      crypto::tink::SerializeKeysetToProtoKeysetFormat(
          *keyset_handle, crypto::tink::InsecureSecretKeyAccess::Get());
  if (!serialized.ok()) {
    return serialized.status();
  }

  return std::string(crypto::tink::util::SecretDataAsStringView(*serialized));
}

RunContext MakeRunContextWithReleaseKey(absl::string_view release_key) {
  RunContext run_context;
  run_context.set_release_key(std::string(release_key));
  return run_context;
}

absl::StatusOr<ReleaseEncrypter> CreateTestEncrypter() {
  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  if (!release_key.ok()) {
    return release_key.status();
  }
  return ReleaseEncrypter::Create(MakeRunContextWithReleaseKey(*release_key));
}

struct RoundTripTestCase {
  std::string plaintext;
  std::string associated_data;
};

class ReleaseEncrypterTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { ASSERT_THAT(crypto::tink::AeadConfig::Register(), IsOk()); }
};

TEST_F(ReleaseEncrypterTest, CreateRejectsEmptyAndInvalidKeysets) {
  EXPECT_THAT(ReleaseEncrypter::Create(MakeRunContextWithReleaseKey("")),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ReleaseEncrypter::Create(MakeRunContextWithReleaseKey(kValid32ByteRawKey)),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(ReleaseEncrypter::Create(MakeRunContextWithReleaseKey("not a valid tink keyset")),
              StatusIs(absl::StatusCode::kInvalidArgument));
  // Valid Keyset protobuf with primary_key_id = 1 but 0 key entries.
  EXPECT_THAT(ReleaseEncrypter::Create(MakeRunContextWithReleaseKey("\x08\x01")),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ReleaseEncrypterTest, CreateRejectsKeysetsWithWrongTemplateOrParameters) {
  // 1. Keyset with TINK prefix (Aes256Gcm instead of Aes256GcmNoPrefix).
  const absl::StatusOr<std::unique_ptr<crypto::tink::KeysetHandle>> tink_prefix_handle =
      crypto::tink::KeysetHandle::GenerateNew(crypto::tink::AeadKeyTemplates::Aes256Gcm(),
                                              crypto::tink::KeyGenConfigGlobalRegistry());
  ASSERT_THAT(tink_prefix_handle, IsOk());
  const absl::StatusOr<crypto::tink::SecretData> tink_prefix_serialized =
      crypto::tink::SerializeKeysetToProtoKeysetFormat(
          **tink_prefix_handle, crypto::tink::InsecureSecretKeyAccess::Get());
  ASSERT_THAT(tink_prefix_serialized, IsOk());
  EXPECT_THAT(ReleaseEncrypter::Create(MakeRunContextWithReleaseKey(
                  crypto::tink::util::SecretDataAsStringView(*tink_prefix_serialized))),
              StatusIs(absl::StatusCode::kInvalidArgument));

  // 2. Keyset with 16-byte (128-bit) key instead of 32-byte (256-bit) key.
  const absl::StatusOr<std::unique_ptr<crypto::tink::KeysetHandle>> aes128_handle =
      crypto::tink::KeysetHandle::GenerateNew(crypto::tink::AeadKeyTemplates::Aes128GcmNoPrefix(),
                                              crypto::tink::KeyGenConfigGlobalRegistry());
  ASSERT_THAT(aes128_handle, IsOk());
  const absl::StatusOr<crypto::tink::SecretData> aes128_serialized =
      crypto::tink::SerializeKeysetToProtoKeysetFormat(
          **aes128_handle, crypto::tink::InsecureSecretKeyAccess::Get());
  ASSERT_THAT(aes128_serialized, IsOk());
  EXPECT_THAT(ReleaseEncrypter::Create(MakeRunContextWithReleaseKey(
                  crypto::tink::util::SecretDataAsStringView(*aes128_serialized))),
              StatusIs(absl::StatusCode::kInvalidArgument));

  // 3. Keyset with non-AES-GCM primitive (XChaCha20Poly1305).
  const absl::StatusOr<std::unique_ptr<crypto::tink::KeysetHandle>> xchacha_handle =
      crypto::tink::KeysetHandle::GenerateNew(crypto::tink::AeadKeyTemplates::XChaCha20Poly1305(),
                                              crypto::tink::KeyGenConfigGlobalRegistry());
  ASSERT_THAT(xchacha_handle, IsOk());
  const absl::StatusOr<crypto::tink::SecretData> xchacha_serialized =
      crypto::tink::SerializeKeysetToProtoKeysetFormat(
          **xchacha_handle, crypto::tink::InsecureSecretKeyAccess::Get());
  ASSERT_THAT(xchacha_serialized, IsOk());
  EXPECT_THAT(ReleaseEncrypter::Create(MakeRunContextWithReleaseKey(
                  crypto::tink::util::SecretDataAsStringView(*xchacha_serialized))),
              StatusIs(absl::StatusCode::kInvalidArgument));

  // 4. Keyset containing multiple keys.
  const absl::StatusOr<std::unique_ptr<crypto::tink::Parameters>> template_params =
      crypto::tink::ParseParametersFromProtoFormat(
          crypto::tink::AeadKeyTemplates::Aes256GcmNoPrefix().SerializeAsString());
  ASSERT_THAT(template_params, IsOk());
  const absl::StatusOr<crypto::tink::KeysetHandle> multi_key_handle =
      crypto::tink::KeysetHandleBuilder()
          .AddEntry(crypto::tink::KeysetHandleBuilder::Entry::CreateFromParams(
              (*template_params)->Clone(), crypto::tink::KeyStatus::kEnabled, /*is_primary=*/true))
          .AddEntry(crypto::tink::KeysetHandleBuilder::Entry::CreateFromParams(
              (*template_params)->Clone(), crypto::tink::KeyStatus::kEnabled,
              /*is_primary=*/false))
          .Build();
  ASSERT_THAT(multi_key_handle, IsOk());
  const absl::StatusOr<crypto::tink::SecretData> multi_key_serialized =
      crypto::tink::SerializeKeysetToProtoKeysetFormat(
          *multi_key_handle, crypto::tink::InsecureSecretKeyAccess::Get());
  ASSERT_THAT(multi_key_serialized, IsOk());
  EXPECT_THAT(ReleaseEncrypter::Create(MakeRunContextWithReleaseKey(
                  crypto::tink::util::SecretDataAsStringView(*multi_key_serialized))),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ReleaseEncrypterTest, CreateSucceedsWithValidSerializedKeyset) {
  const absl::StatusOr<std::string> serialized_keyset =
      SerializeRawAesGcmKeyToKeyset(kValid32ByteRawKey);
  ASSERT_THAT(serialized_keyset, IsOk());

  const absl::StatusOr<ReleaseEncrypter> encrypter =
      ReleaseEncrypter::Create(MakeRunContextWithReleaseKey(*serialized_keyset));

  ASSERT_THAT(encrypter, IsOk());

  EXPECT_THAT(encrypter->ReleaseKey(), Eq(*serialized_keyset));
  EXPECT_THAT(crypto::tink::ParseKeysetFromProtoKeysetFormat(
                  encrypter->ReleaseKey(), crypto::tink::InsecureSecretKeyAccess::Get()),
              IsOk());
}

TEST_F(ReleaseEncrypterTest, EncryptWithOneEncrypterThenDeserializeKeyAndDecryptWithAnother) {
  const absl::StatusOr<std::string> release_key = ReleaseEncrypter::CreateReleaseKey();
  ASSERT_THAT(release_key, IsOk());

  const absl::StatusOr<ReleaseEncrypter> encrypter =
      ReleaseEncrypter::Create(MakeRunContextWithReleaseKey(*release_key));
  ASSERT_THAT(encrypter, IsOk());

  const std::string plaintext = "confidential message encrypted upstream";
  const std::string associated_data = "routing_metadata_ad";

  const absl::StatusOr<std::string> ciphertext =
      encrypter->EncryptForRelease(plaintext, associated_data);
  ASSERT_THAT(ciphertext, IsOk());

  // Serialize the release keyset and deserialize it in a separate encrypter instance.
  const std::string serialized_key = encrypter->ReleaseKey();
  const absl::StatusOr<ReleaseEncrypter> decrypter =
      ReleaseEncrypter::Create(MakeRunContextWithReleaseKey(serialized_key));
  ASSERT_THAT(decrypter, IsOk());

  const absl::StatusOr<std::string> decrypted =
      decrypter->DecryptReleased(*ciphertext, associated_data);
  ASSERT_THAT(decrypted, IsOk());
  EXPECT_THAT(*decrypted, Eq(plaintext));
}

TEST_F(ReleaseEncrypterTest, CreateReleaseKeySucceedsAndGeneratesUniqueKeysets) {
  const absl::StatusOr<std::string> key1 = ReleaseEncrypter::CreateReleaseKey();
  const absl::StatusOr<std::string> key2 = ReleaseEncrypter::CreateReleaseKey();

  ASSERT_THAT(key1, IsOk());
  ASSERT_THAT(key2, IsOk());

  EXPECT_THAT(*key1, Not(IsEmpty()));
  EXPECT_THAT(*key2, Not(IsEmpty()));
  EXPECT_THAT(*key1, Ne(*key2));

  EXPECT_THAT(crypto::tink::ParseKeysetFromProtoKeysetFormat(
                  *key1, crypto::tink::InsecureSecretKeyAccess::Get()),
              IsOk());
  EXPECT_THAT(crypto::tink::ParseKeysetFromProtoKeysetFormat(
                  *key2, crypto::tink::InsecureSecretKeyAccess::Get()),
              IsOk());

  const absl::StatusOr<ReleaseEncrypter> encrypter1 =
      ReleaseEncrypter::Create(MakeRunContextWithReleaseKey(*key1));
  const absl::StatusOr<ReleaseEncrypter> encrypter2 =
      ReleaseEncrypter::Create(MakeRunContextWithReleaseKey(*key2));

  ASSERT_THAT(encrypter1, IsOk());
  ASSERT_THAT(encrypter2, IsOk());

  EXPECT_THAT(encrypter1->ReleaseKey(), Eq(*key1));
  EXPECT_THAT(encrypter2->ReleaseKey(), Eq(*key2));
}

TEST_F(ReleaseEncrypterTest, EncryptForReleaseAndDecryptReleasedRoundTrip) {
  const absl::StatusOr<ReleaseEncrypter> encrypter = CreateTestEncrypter();
  ASSERT_THAT(encrypter, IsOk());

  const std::vector<RoundTripTestCase> test_cases = {
      {.plaintext = "", .associated_data = ""},
      {.plaintext = "hello world", .associated_data = ""},
      {.plaintext = "", .associated_data = "authenticated_metadata"},
      {.plaintext = "confidential message payload", .associated_data = "routing_partition_key"},
  };

  for (const RoundTripTestCase& test_case : test_cases) {
    const absl::StatusOr<std::string> ciphertext =
        encrypter->EncryptForRelease(test_case.plaintext, test_case.associated_data);
    ASSERT_THAT(ciphertext, IsOk());

    EXPECT_THAT(*ciphertext, SizeIs(kAesGcmIvSize + test_case.plaintext.size() + kAesGcmTagSize));

    const absl::StatusOr<std::string> decrypted =
        encrypter->DecryptReleased(*ciphertext, test_case.associated_data);
    ASSERT_THAT(decrypted, IsOk());

    EXPECT_THAT(*decrypted, Eq(test_case.plaintext));
  }
}

TEST_F(ReleaseEncrypterTest, EncryptForReleaseWithEmptyAssociatedData) {
  const absl::StatusOr<ReleaseEncrypter> encrypter = CreateTestEncrypter();
  ASSERT_THAT(encrypter, IsOk());

  const absl::StatusOr<std::string> ciphertext = encrypter->EncryptForRelease("payload", "");
  ASSERT_THAT(ciphertext, IsOk());

  const absl::StatusOr<std::string> decrypted = encrypter->DecryptReleased(*ciphertext, "");
  ASSERT_THAT(decrypted, IsOk());

  EXPECT_THAT(*decrypted, Eq("payload"));
}

TEST_F(ReleaseEncrypterTest, EncryptGeneratesUniqueIvEachTime) {
  const absl::StatusOr<ReleaseEncrypter> encrypter = CreateTestEncrypter();
  ASSERT_THAT(encrypter, IsOk());

  const absl::StatusOr<std::string> ciphertext1 =
      encrypter->EncryptForRelease("same payload", "ad");
  const absl::StatusOr<std::string> ciphertext2 =
      encrypter->EncryptForRelease("same payload", "ad");

  ASSERT_THAT(ciphertext1, IsOk());
  ASSERT_THAT(ciphertext2, IsOk());

  EXPECT_THAT(*ciphertext1, Ne(*ciphertext2));

  // The 12-byte IVs at the beginning must differ.
  EXPECT_THAT(ciphertext1->substr(0, kAesGcmIvSize), Ne(ciphertext2->substr(0, kAesGcmIvSize)));
}

TEST_F(ReleaseEncrypterTest, DecryptFailsWithCorruptedIv) {
  const absl::StatusOr<ReleaseEncrypter> encrypter = CreateTestEncrypter();
  ASSERT_THAT(encrypter, IsOk());

  const absl::StatusOr<std::string> ciphertext =
      encrypter->EncryptForRelease("sensitive payload", "ad");
  ASSERT_THAT(ciphertext, IsOk());

  std::string corrupted_iv = *ciphertext;
  corrupted_iv[0] ^= 0x01;
  EXPECT_THAT(encrypter->DecryptReleased(corrupted_iv, "ad"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ReleaseEncrypterTest, DecryptFailsWithCorruptedPayload) {
  const absl::StatusOr<ReleaseEncrypter> encrypter = CreateTestEncrypter();
  ASSERT_THAT(encrypter, IsOk());

  const absl::StatusOr<std::string> ciphertext =
      encrypter->EncryptForRelease("sensitive payload", "ad");
  ASSERT_THAT(ciphertext, IsOk());

  std::string corrupted_payload = *ciphertext;
  corrupted_payload[kAesGcmIvSize] ^= 0x01;
  EXPECT_THAT(encrypter->DecryptReleased(corrupted_payload, "ad"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ReleaseEncrypterTest, DecryptFailsWithCorruptedTag) {
  const absl::StatusOr<ReleaseEncrypter> encrypter = CreateTestEncrypter();
  ASSERT_THAT(encrypter, IsOk());

  const absl::StatusOr<std::string> ciphertext =
      encrypter->EncryptForRelease("sensitive payload", "ad");
  ASSERT_THAT(ciphertext, IsOk());

  std::string corrupted_tag = *ciphertext;
  corrupted_tag.back() ^= 0x01;
  EXPECT_THAT(encrypter->DecryptReleased(corrupted_tag, "ad"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ReleaseEncrypterTest, DecryptFailsWithTruncatedCiphertext) {
  const absl::StatusOr<ReleaseEncrypter> encrypter = CreateTestEncrypter();
  ASSERT_THAT(encrypter, IsOk());

  constexpr std::size_t kMinimumValidCiphertextSize = kAesGcmIvSize + kAesGcmTagSize;  // 28 bytes

  // Truncations below minimum valid ciphertext size.
  for (const std::size_t truncated_size : {0, 1, 8, 12, 16, 27}) {
    const std::string truncated_ciphertext(truncated_size, 'c');
    EXPECT_THAT(encrypter->DecryptReleased(truncated_ciphertext, "ad"),
                StatusIs(absl::StatusCode::kInvalidArgument));
  }

  // Truncations of a valid ciphertext.
  const absl::StatusOr<std::string> ciphertext =
      encrypter->EncryptForRelease("multi-character test message", "ad");
  ASSERT_THAT(ciphertext, IsOk());

  for (std::size_t cut = 1; cut <= kMinimumValidCiphertextSize; ++cut) {
    const std::string truncated = ciphertext->substr(0, ciphertext->size() - cut);
    EXPECT_THAT(encrypter->DecryptReleased(truncated, "ad"),
                StatusIs(absl::StatusCode::kInvalidArgument));
  }
}

TEST_F(ReleaseEncrypterTest, DecryptFailsIfAssociatedDataDiffers) {
  const absl::StatusOr<ReleaseEncrypter> encrypter = CreateTestEncrypter();
  ASSERT_THAT(encrypter, IsOk());

  const absl::StatusOr<std::string> ciphertext =
      encrypter->EncryptForRelease("sensitive payload", "expected_ad");
  ASSERT_THAT(ciphertext, IsOk());

  EXPECT_THAT(encrypter->DecryptReleased(*ciphertext, "tampered_ad"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(encrypter->DecryptReleased(*ciphertext, ""),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ReleaseEncrypterTest, DistinctKeysProduceDistinctCiphertexts) {
  const absl::StatusOr<ReleaseEncrypter> encrypter1 = CreateTestEncrypter();
  const absl::StatusOr<ReleaseEncrypter> encrypter2 = CreateTestEncrypter();

  ASSERT_THAT(encrypter1, IsOk());
  ASSERT_THAT(encrypter2, IsOk());

  const absl::StatusOr<std::string> ciphertext1 =
      encrypter1->EncryptForRelease("same payload", "ad");
  const absl::StatusOr<std::string> ciphertext2 =
      encrypter2->EncryptForRelease("same payload", "ad");

  ASSERT_THAT(ciphertext1, IsOk());
  ASSERT_THAT(ciphertext2, IsOk());

  EXPECT_THAT(*ciphertext1, Ne(*ciphertext2));

  // Cross-decryption must fail.
  EXPECT_THAT(encrypter1->DecryptReleased(*ciphertext2, "ad"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(encrypter2->DecryptReleased(*ciphertext1, "ad"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(ReleaseEncrypterTest, SupportsMoveConstruction) {
  absl::StatusOr<ReleaseEncrypter> original = CreateTestEncrypter();
  ASSERT_THAT(original, IsOk());
  const std::string original_key = original->ReleaseKey();

  const absl::StatusOr<std::string> ciphertext =
      original->EncryptForRelease("payload to move", "ad");
  ASSERT_THAT(ciphertext, IsOk());

  // Test move construction.
  ReleaseEncrypter moved_encrypter(std::move(*original));
  EXPECT_THAT(moved_encrypter.ReleaseKey(), Eq(original_key));

  const absl::StatusOr<std::string> decrypted1 = moved_encrypter.DecryptReleased(*ciphertext, "ad");
  ASSERT_THAT(decrypted1, IsOk());
  EXPECT_THAT(*decrypted1, Eq("payload to move"));

  // Moved-from object must reject operations with kFailedPrecondition.
  EXPECT_THAT(original->EncryptForRelease("new payload", ""),
              StatusIs(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(original->DecryptReleased(*ciphertext, ""),
              StatusIs(absl::StatusCode::kFailedPrecondition));
}

TEST_F(ReleaseEncrypterTest, MultiBlockPayloadRoundTrip) {
  const absl::StatusOr<ReleaseEncrypter> encrypter = CreateTestEncrypter();
  ASSERT_THAT(encrypter, IsOk());

  const std::string large_payload(4096, 'x');
  const std::string associated_data = "large_payload_metadata";

  const absl::StatusOr<std::string> ciphertext =
      encrypter->EncryptForRelease(large_payload, associated_data);
  ASSERT_THAT(ciphertext, IsOk());
  EXPECT_THAT(*ciphertext, SizeIs(kAesGcmIvSize + large_payload.size() + kAesGcmTagSize));

  const absl::StatusOr<std::string> decrypted =
      encrypter->DecryptReleased(*ciphertext, associated_data);
  ASSERT_THAT(decrypted, IsOk());
  EXPECT_THAT(*decrypted, Eq(large_payload));
}

}  // namespace
}  // namespace cobalt::trusted_shuffler
