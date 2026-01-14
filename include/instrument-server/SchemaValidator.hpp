#pragma once

#include "instrument-server/export.h"

#include "types.hpp"

namespace instserver {

class INSTRUMENT_SERVER_API SchemaValidator {
public:
  // Validate YAML against schemas
  static ValidationResult validate_instrument_api(const std::string &yaml_path);
  static ValidationResult
  validate_quantum_dot_device(const std::string &yaml_path);
  static ValidationResult
  validate_instrument_configuration(const std::string &yaml_path);

  // Get embedded schemas
  static std::string get_instrument_api_schema();
  static std::string get_instrument_configuration_schema();
};

} // namespace instserver
