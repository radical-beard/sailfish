#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "platform/child_process.h"

int main(int argc, char** argv) {
  sailfish::platform::install_process_defaults();
  return doctest::Context(argc, argv).run();
}
