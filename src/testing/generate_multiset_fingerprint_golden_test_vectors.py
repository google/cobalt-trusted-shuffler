#!/usr/bin/env python3
# Copyright 2026 Google LLC.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Generates golden test vectors for MSet-Add-Hash multiset fingerprinting.

Usage & Purpose:
  This script is a developer utility used to generate and reproduce
  the golden test vectors verified in C++ unit tests
  (MultisetFingerprintTest.GoldenTestCaseMSetAddHash in
  src/crypto/multiset_fingerprint_test.cc).
  It outputs the generated test vectors into a checked-in JSON test data file
  (src/crypto/testdata/multiset_fingerprint_golden_test_vectors.json by default) which is loaded
  as a data dependency by the C++ unit test.

This script uses the open-source `pymsh` (Python Multiset Hashing) library's
MSetAddHash implementation, adapted for the parameters agreed upon with ISE
Crypto:
  - PRF: HMAC-SHA256 (instead of default HMAC-BLAKE2b)
  - Modulus: 2^256 (m = 256 bits)
  - Group operation: Addition modulo 2^256 over 32-byte big-endian integers
  - No prefix byte or nonce in hash input

How to run:
  bazel run //src/testing:generate_multiset_fingerprint_golden_test_vectors
"""

import argparse
import hashlib
import hmac
import json
import os
import sys
from typing import List

try:
  from pymsh import MSetAddHash
except ImportError as e:
  raise SystemExit(
      "Missing dependency 'pymsh'. Please install it via:\n"
      "  pip install pymsh\n"
      "or run within an environment where pymsh is installed."
  ) from e

KEY: bytes = b"golden-test-run-hash-key-32byte"

MESSAGES: List[bytes] = [
    b"message-alpha-001",
    b"message-beta-002",
    b"message-gamma-003",
    b"message-delta-004",
    b"message-epsilon-005",
    b"message-zeta-006",
    b"message-eta-007",
    b"message-theta-008",
    b"message-iota-009",
    b"message-kappa-010",
]


class MSetAddHashSha256(MSetAddHash):
  """Adapts pymsh.MSetAddHash to HMAC-SHA256 modulo 2^256.

  Overrides the PRF (_H), accumulator initialization, and merge operations
  to match the Trusted Shuffler multiset fingerprinting specification:
    - HMAC-SHA256 over the element bytes without prefix
    - Initial accumulator = 0 (no random nonce seed)
    - Big-endian 32-byte integer addition modulo 2^256
  """

  def __init__(self, key: bytes):
    self.key = key
    self.m = 256
    self.nonce = b""
    self.acc = 0
    self.count = 0

  def _H(self, prefix: int, data: bytes) -> int:
    """Computes HMAC-SHA256(key, data) as a 256-bit big-endian integer."""
    raw = hmac.new(self.key, data, hashlib.sha256).digest()
    return int.from_bytes(raw, byteorder="big")

  def update(self, element: bytes, multiplicity: int = 1) -> None:
    """Adds element copies to the accumulator and tracks count."""
    super().update(element, multiplicity)
    self.count += multiplicity

  def add_message(self, message: bytes, multiplicity: int = 1) -> None:
    """Convenience alias for update()."""
    self.update(message, multiplicity)

  def merge(self, other: "MSetAddHashSha256") -> None:
    """Homomorphically merges another sketch into this one."""
    self.acc = (self.acc + other.acc) % (1 << self.m)
    self.count += other.count

  def digest(self) -> bytes:
    """Returns the 32-byte big-endian representation of the accumulator."""
    return self.acc.to_bytes(self.m // 8, byteorder="big")


def get_default_output_path() -> str:
  workspace_dir = os.environ.get("BUILD_WORKSPACE_DIRECTORY", "")
  if not workspace_dir:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    workspace_dir = os.path.abspath(os.path.join(script_dir, "../.."))
  return os.path.join(
      workspace_dir, "src/crypto/testdata/multiset_fingerprint_golden_test_vectors.json"
  )


def main() -> None:
  parser = argparse.ArgumentParser(
      description="Generate golden test vectors for MSet-Add-Hash multiset fingerprinting."
  )
  parser.add_argument(
      "--output",
      default=get_default_output_path(),
      help="Output JSON file path (default: src/crypto/testdata/multiset_fingerprint_golden_test_vectors.json)",
  )
  args = parser.parse_args()

  print("=" * 72)
  print("MSet-Add-Hash Golden Test Vector Generator (using pymsh)")
  print("Parameters: HMAC-SHA256, Addition modulo 2^256, 32-byte output")
  print("Key:", KEY.decode("ascii"))
  print("=" * 72)

  # Sketch 1: Add first 5 messages
  sketch1 = MSetAddHashSha256(KEY)
  for msg in MESSAGES[:5]:
    sketch1.update(msg)
  digest1 = sketch1.digest()
  print("\n[Sketch 1] (5 messages: msgs 0..4)")
  print("  Count:  %d" % sketch1.count)
  print("  Digest: %s" % digest1.hex())

  # Sketch 2: Add next 5 messages
  sketch2 = MSetAddHashSha256(KEY)
  for msg in MESSAGES[5:]:
    sketch2.update(msg)
  digest2 = sketch2.digest()
  print("\n[Sketch 2] (5 messages: msgs 5..9)")
  print("  Count:  %d" % sketch2.count)
  print("  Digest: %s" % digest2.hex())

  # Merged Sketch: Merge Sketch 1 and Sketch 2
  merged = MSetAddHashSha256(KEY)
  merged.merge(sketch1)
  merged.merge(sketch2)
  digest_merged = merged.digest()
  print("\n[Merged Sketch] (Sketch 1 + Sketch 2)")
  print("  Count:  %d" % merged.count)
  print("  Digest: %s" % digest_merged.hex())

  output_data = {
      "key": KEY.decode("ascii"),
      "sketch1_messages": [msg.decode("ascii") for msg in MESSAGES[:5]],
      "sketch2_messages": [msg.decode("ascii") for msg in MESSAGES[5:]],
      "expected_sketch1_digest_hex": digest1.hex(),
      "expected_sketch2_digest_hex": digest2.hex(),
      "expected_merged_digest_hex": digest_merged.hex(),
  }

  output_path = os.path.abspath(args.output)
  os.makedirs(os.path.dirname(output_path), exist_ok=True)
  with open(output_path, "w", encoding="utf-8") as f:
    json.dump(output_data, f, indent=2)
    f.write("\n")

  print("\n" + "=" * 72)
  print("Successfully generated golden test vectors file:")
  print(" ", output_path)
  print("=" * 72)


if __name__ == "__main__":
  main()

