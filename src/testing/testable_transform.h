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

#ifndef SRC_TESTING_TESTABLE_TRANSFORM_H_
#define SRC_TESTING_TESTABLE_TRANSFORM_H_

#include <concepts>

#include "containers/confidential_transform_server_base.h"

namespace cobalt::trusted_shuffler::testing {

template <typename T>
  requires std::derived_from<T, confidential_federated_compute::ConfidentialTransformBase>
class TestableTransform : public T {
 public:
  using T::T;

  // Expose protected members of T used in tests.
  using T::CreateSession;
  using T::GetKeyId;
  using T::ReadWriteConfigurationRequest;
  using T::StreamInitializeTransform;
};

}  // namespace cobalt::trusted_shuffler::testing

#endif  // SRC_TESTING_TESTABLE_TRANSFORM_H_