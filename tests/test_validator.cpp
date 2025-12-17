#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

namespace fs = std::filesystem;

// Helper to expand template using the template_expander tool
std::string expand_template(const std::string &tmpl_path) {
  // Create a temp file for the expanded output
  std::string tmp_dir = fs::temp_directory_path();
  std::string expanded_path = tmp_dir + "/dso9254a_expanded.yaml";
  std::string cmd = "template_expander " + tmpl_path + " " + expanded_path;
  int ret = std::system(cmd.c_str());
  if (ret != 0) {
    throw std::runtime_error("Failed to expand template: " + tmpl_path);
  }
  return expanded_path;
}

// Helper to run a CLI validator tool
int run_validator(const std::string &tool, const std::string &yaml_path) {
  std::string cmd = tool + " " + yaml_path + " > /dev/null 2>&1";
  return std::system(cmd.c_str());
}

TEST(SchemaValidatorTest, ValidateAgilentInstrumentDirect) {
  int ret = run_validator("validate_instrument_api",
                          "examples/instrument-apis/agi_34401a.yaml");
  EXPECT_EQ(ret, 0) << "Validation failed for Agilent instrument API";
}

TEST(SchemaValidatorTest, ValidateAgilentInstrumentWithExpander) {
  // ... expand_template as before ...
  std::string expanded_path =
      expand_template("examples/instrument-apis/agi_34401a.yaml");
  int ret = run_validator("validate_instrument_api", expanded_path);
  EXPECT_EQ(ret, 0) << "Validation failed for expanded Agilent instrument API";
  std::remove(expanded_path.c_str());
}

TEST(SchemaValidatorTest, ValidateKeysightInstrument) {
  std::string expanded_path =
      expand_template("examples/instrument-apis/dso9254a.yaml.tmpl");
  int ret = run_validator("validate_instrument_api", expanded_path);
  EXPECT_EQ(ret, 0) << "Validation failed for expanded Keysight instrument API";
  std::remove(expanded_path.c_str());
}

TEST(SchemaValidatorTest, ValidateSystemContext) {
  int ret =
      run_validator("validate_system_context", "examples/system_context.yaml");
  EXPECT_EQ(ret, 0) << "Validation failed for system context";
}

TEST(SchemaValidatorTest, ValidateQuantumDotDeviceConfig) {
  int ret = run_validator("validate_quantum_dot_config",
                          "examples/one_charge_sensor_quantum_dot_device.yaml");
  EXPECT_EQ(ret, 0) << "Validation failed for quantum dot device config";
}

TEST(SchemaValidatorTest, GenerateAndValidateAgilentInstrumentConfiguration) {
  std::string api_path = "examples/instrument-apis/agi_34401a.yaml";
  std::string tmp_dir = fs::temp_directory_path();
  std::string config_path = tmp_dir + "/agi_34401a_config.yaml";
  std::string cmd =
      "instrument_configuration_generator " + api_path + " " + config_path;
  int ret = std::system(cmd.c_str());
  ASSERT_EQ(ret, 0)
      << "Failed to generate instrument configuration for Agilent";
  ret = run_validator("validate_instrument_configuration", config_path);
  EXPECT_EQ(ret, 0)
      << "Validation failed for generated Agilent instrument configuration";
  std::remove(config_path.c_str());
}

TEST(SchemaValidatorTest, GenerateAndValidateKeysightInstrumentConfiguration) {
  std::string api_path =
      expand_template("examples/instrument-apis/dso9254a.yaml.tmpl");
  std::string tmp_dir = fs::temp_directory_path();
  std::string config_path = tmp_dir + "/dso9254a_config.yaml";
  std::string cmd =
      "instrument_configuration_generator " + api_path + " " + config_path;
  int ret = std::system(cmd.c_str());
  ASSERT_EQ(ret, 0)
      << "Failed to generate instrument configuration for Keysight";
  ret = run_validator("validate_instrument_configuration", config_path);
  EXPECT_EQ(ret, 0)
      << "Validation failed for generated Keysight instrument configuration";
  std::remove(api_path.c_str());
  std::remove(config_path.c_str());
}
