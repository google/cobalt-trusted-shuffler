#ifndef SRC_TESTING_TESTABLE_TRANSFORM_H_
#define SRC_TESTING_TESTABLE_TRANSFORM_H_

#include <concepts>

#include "containers/confidential_transform_server_base.h"

namespace cobalt::trusted_shuffler::testing {

template <typename T>
  requires std::derived_from<
      T, confidential_federated_compute::ConfidentialTransformBase>
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