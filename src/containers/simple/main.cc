#include <cstdlib>
#include <iostream>

#include "src/containers/simple/session.h"

int main() {
  cobalt::trusted_shuffler::Session session;
  std::cout << "Hello, Trusted Shuffler!\n";
  std::cout << "Instantiated a session\n";
  return EXIT_SUCCESS;
}