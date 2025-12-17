#pragma once

#include "types.hpp"
#include <string>
#include <vector>

namespace instserver {

struct ValidationError {
  std::string path;
  std::string message;
  int line;
  int column;
};

struct ValidationResult {
  bool valid;
  std::vector<ValidationError> errors;
  std::vector<std::string> warnings;
};

class SchemaValidator {
public:
  // Validate YAML against schemas
  static ValidationResult validate_instrument_api(const std::string &yaml_path);
  static ValidationResult validate_system_context(const std::string &yaml_path);
  static ValidationResult
  validate_quantum_dot_device(const std::string &yaml_path);
  static ValidationResult
  validate_instrument_configuration(const std::string &yaml_path);

  // Parse YAML into structs
  static InstrumentAPI parse_instrument_api(const std::string &yaml_path);
  static std::map<std::string, RuntimeContext>
  parse_runtime_contexts(const std::string &yaml_path);

  // Get embedded schemas
  static std::string get_instrument_api_schema();
  static std::string get_system_context_schema();
  static std::string get_runtime_contexts_schema();
};

} // namespace instserver
