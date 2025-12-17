#include <instrument-server/SchemaValidator.hpp>
#include <iostream>
#include <yaml-cpp/yaml.h>
using namespace instserver;

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <input.yaml>\n";
    return 1;
  }
  auto result = SchemaValidator::validate_instrument_configuration(argv[1]);
  if (result.valid) {
    std::cout << "Validation succeeded.\n";
    return 0;
  } else {
    std::cout << "Validation failed:\n";
    for (const auto &err : result.errors) {
      std::cout << "  - " << err.path << ": " << err.message << "\n";
    }
    return 2;
  }
}
