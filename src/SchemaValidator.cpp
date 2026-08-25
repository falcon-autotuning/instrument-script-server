#include "instrument-script-server/SchemaValidator.hpp"

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <nlohmann/json_fwd.hpp>
#include <stdexcept>
#include <string>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

namespace instserver {

// Embedded JSON Schema strings.
extern const char INSTRUMENT_API_SCHEMA[];
extern const char INSTRUMENT_CONFIGURATION_SCHEMA[];
extern const char QUANTUM_DOT_DEVICE_SCHEMA[];

namespace {

using Json = nlohmann::json;
using JsonValidator = nlohmann::json_schema::json_validator;
using JsonPointer = Json::json_pointer;

// -----------------------------------------------------------------------------
// YAML-to-JSON conversion
// -----------------------------------------------------------------------------
//
// json-schema-validator validates nlohmann::json values. It does not accept
// YAML::Node directly, so this conversion is required.
//
// This function does not perform schema validation. It only preserves the
// document's object, array, scalar, and null values as JSON.
// -----------------------------------------------------------------------------

bool try_parse_integer(const std::string &text, std::int64_t &result) {
  if (text.empty()) {
    return false;
  }

  char *end = nullptr;
  errno = 0;

  const long long value = std::strtoll(text.c_str(), &end, 10);

  if (errno == ERANGE || end == text.c_str() || end == nullptr ||
      *end != '\0') {
    return false;
  }

  result = static_cast<std::int64_t>(value);
  return true;
}

bool try_parse_number(const std::string &text, double &result) {
  if (text.empty()) {
    return false;
  }

  // Leave integer-looking strings to try_parse_integer().
  if (text.find_first_of(".eE") == std::string::npos) {
    return false;
  }

  char *end = nullptr;
  errno = 0;

  const double value = std::strtod(text.c_str(), &end);

  if (errno == ERANGE || end == text.c_str() || end == nullptr ||
      *end != '\0' || !std::isfinite(value)) {
    return false;
  }

  result = value;
  return true;
}

Json yaml_scalar_to_json(const YAML::Node &node) {
  const std::string value = node.Scalar();
  const std::string tag = node.Tag();

  // Respect explicitly tagged YAML values first.
  if (tag == "tag:yaml.org,2002:null" || tag == "!!null") {
    return nullptr;
  }

  if (tag == "tag:yaml.org,2002:str" || tag == "!!str" || tag == "!") {
    return value;
  }

  if (tag == "tag:yaml.org,2002:bool" || tag == "!!bool") {
    return node.as<bool>();
  }

  if (tag == "tag:yaml.org,2002:int" || tag == "!!int") {
    return node.as<std::int64_t>();
  }

  if (tag == "tag:yaml.org,2002:float" || tag == "!!float") {
    return node.as<double>();
  }

  // Resolve common untagged YAML scalars into JSON scalar types.
  if (value == "null" || value == "Null" || value == "NULL" || value == "~") {
    return nullptr;
  }

  if (value == "true" || value == "True" || value == "TRUE") {
    return true;
  }

  if (value == "false" || value == "False" || value == "FALSE") {
    return false;
  }

  std::int64_t integer_value = 0;

  if (try_parse_integer(value, integer_value)) {
    return integer_value;
  }

  double number_value = 0.0;

  if (try_parse_number(value, number_value)) {
    return number_value;
  }

  return value;
}

Json yaml_to_json(const YAML::Node &node) {
  if (!node || node.IsNull()) {
    return nullptr;
  }

  if (node.IsScalar()) {
    return yaml_scalar_to_json(node);
  }

  if (node.IsSequence()) {
    Json result = Json::array();

    for (const YAML::Node &entry : node) {
      result.push_back(yaml_to_json(entry));
    }

    return result;
  }

  if (node.IsMap()) {
    Json result = Json::object();

    for (const auto &entry : node) {
      if (!entry.first.IsScalar()) {
        throw std::runtime_error("YAML object keys must be scalar strings");
      }

      const std::string key = entry.first.as<std::string>();
      result[key] = yaml_to_json(entry.second);
    }

    return result;
  }

  throw std::runtime_error("Unsupported YAML node type");
}

// -----------------------------------------------------------------------------
// ValidationResult helpers
// -----------------------------------------------------------------------------

void add_error(ValidationResult &result, const std::string &path,
               const std::string &message, int line = 0, int column = 0) {
  result.valid = false;
  result.errors.push_back({path, message, line, column});
}

// -----------------------------------------------------------------------------
// json-schema-validator error adapter
// -----------------------------------------------------------------------------
//
// json-schema-validator invokes this callback for every schema validation
// failure. This is the only adaptation between the library's errors and the
// project's ValidationResult type.
// -----------------------------------------------------------------------------

class ValidationErrorHandler final
    : public nlohmann::json_schema::basic_error_handler {
public:
  explicit ValidationErrorHandler(ValidationResult &result) : result_(result) {}

  void error(const JsonPointer &pointer, const Json &instance,
             const std::string &message) override {
    // Let basic_error_handler record its internal error state.
    nlohmann::json_schema::basic_error_handler::error(pointer, instance,
                                                      message);

    add_error(result_, pointer.to_string(), message);
  }

private:
  ValidationResult &result_;
};

// -----------------------------------------------------------------------------
// Schema construction
// -----------------------------------------------------------------------------
//
// Parsing here means converting a JSON string to nlohmann::json.
// json-schema-validator still performs all schema compilation and validation.
// -----------------------------------------------------------------------------

std::unique_ptr<JsonValidator>
create_validator(const char *schema_text, const std::string &schema_name) {
  if (schema_text == nullptr) {
    throw std::runtime_error("Embedded " + schema_name + " schema is null");
  }

  Json schema;

  try {
    schema = Json::parse(schema_text);
  } catch (const Json::parse_error &error) {
    throw std::runtime_error(
        "Embedded " + schema_name +
        " schema is not valid JSON: " + std::string(error.what()));
  }

  auto validator = std::make_unique<JsonValidator>();

  // This is where json-schema-validator reads and compiles the schema.
  validator->set_root_schema(schema);

  return validator;
}

const JsonValidator &get_instrument_api_validator() {
  static const std::unique_ptr<JsonValidator> validator =
      create_validator(INSTRUMENT_API_SCHEMA, "instrument API");

  return *validator;
}

const JsonValidator &get_quantum_dot_device_validator() {
  static const std::unique_ptr<JsonValidator> validator =
      create_validator(QUANTUM_DOT_DEVICE_SCHEMA, "quantum-dot device");

  return *validator;
}

const JsonValidator &get_instrument_configuration_validator() {
  static const std::unique_ptr<JsonValidator> validator = create_validator(
      INSTRUMENT_CONFIGURATION_SCHEMA, "instrument configuration");

  return *validator;
}

// -----------------------------------------------------------------------------
// Common validation function
// -----------------------------------------------------------------------------

ValidationResult validate_yaml_file(const std::string &yaml_path,
                                    const JsonValidator &validator) {
  ValidationResult result;
  result.valid = true;

  try {
    // Parse YAML syntax.
    const YAML::Node yaml_document = YAML::LoadFile(yaml_path);

    // Convert the YAML value into the JSON representation expected by
    // json-schema-validator.
    const Json json_document = yaml_to_json(yaml_document);

    // All structural and document-validation rules are handled by the
    // JSON Schema library here.
    ValidationErrorHandler error_handler(result);
    validator.validate(json_document, error_handler);
  } catch (const YAML::BadFile &error) {
    add_error(result, "",
              "Unable to open YAML file '" + yaml_path + "': " + error.what());
  } catch (const YAML::ParserException &error) {
    const int line = error.mark.line >= 0 ? error.mark.line + 1 : 0;

    const int column = error.mark.column >= 0 ? error.mark.column + 1 : 0;

    add_error(result, "", std::string("YAML parse error: ") + error.what(),
              line, column);
  } catch (const YAML::Exception &error) {
    add_error(result, "",
              std::string("YAML processing error: ") + error.what());
  } catch (const Json::parse_error &error) {
    add_error(result, "", std::string("JSON parse error: ") + error.what());
  } catch (const Json::exception &error) {
    add_error(result, "", std::string("JSON error: ") + error.what());
  } catch (const std::exception &error) {
    add_error(result, "",
              std::string("Schema validation error: ") + error.what());
  }

  return result;
}

} // namespace

ValidationResult
SchemaValidator::validate_instrument_api(const std::string &yaml_path) {
  return validate_yaml_file(yaml_path, get_instrument_api_validator());
}

ValidationResult
SchemaValidator::validate_quantum_dot_device(const std::string &yaml_path) {
  return validate_yaml_file(yaml_path, get_quantum_dot_device_validator());
}

ValidationResult SchemaValidator::validate_instrument_configuration(
    const std::string &yaml_path) {
  return validate_yaml_file(yaml_path,
                            get_instrument_configuration_validator());
}

std::string SchemaValidator::get_instrument_api_schema() {
  return std::string(INSTRUMENT_API_SCHEMA);
}

std::string SchemaValidator::get_instrument_configuration_schema() {
  return std::string(INSTRUMENT_CONFIGURATION_SCHEMA);
}

} // namespace instserver
