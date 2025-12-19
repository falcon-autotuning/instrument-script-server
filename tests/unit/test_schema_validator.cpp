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
  std::string cmd = "template-expander " + tmpl_path + " " + expanded_path;
  int ret = std::system(cmd.c_str());
  if (ret != 0) {
    throw std::runtime_error("Failed to expand template: " + tmpl_path);
  }
  return expanded_path;
}

// Helper to expand template using the template_expander tool
std::string generate_configuration(const std::string &tmpl_path) {
  namespace fs = std::filesystem;
  std::string tmp_dir = fs::temp_directory_path();
  std::string expanded_path = tmp_dir + "/dso9254a_config.yaml";
  std::string cmd =
      "generate-instrument-config " + tmpl_path + " " + expanded_path + " 2>&1";
  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    std::cout << buffer; // Forward output to test's stdout
  }
  int ret = pclose(pipe);
  if (ret != 0) {
    throw std::runtime_error("Failed to generate template: " + expanded_path);
  }
  return expanded_path;
}

// Helper to run a CLI validator tool
int run_validator(const std::string &tool, const std::string &yaml_path) {
  std::string cmd = tool + " " + yaml_path + " > /dev/null 2>&1";
  return std::system(cmd.c_str());
}

TEST(SchemaValidatorTest, ValidateAgilentInstrumentDirect) {
  int ret = run_validator("validate-instrument-api",
                          "examples/instrument-apis/agi_34401a.yaml");
  EXPECT_EQ(ret, 0) << "Validation failed for Agilent instrument API";
}

TEST(SchemaValidatorTest, ValidateAgilentInstrumentWithExpander) {
  // ... expand_template as before ...
  std::string expanded_path =
      expand_template("examples/instrument-apis/agi_34401a.yaml");
  int ret = run_validator("validate-instrument-api", expanded_path);
  EXPECT_EQ(ret, 0) << "Validation failed for expanded Agilent instrument API";
}

TEST(SchemaValidatorTest, ValidateKeysightInstrument) {
  std::string expanded_path =
      expand_template("examples/instrument-apis/dso9254a.yaml.tmpl");
  int ret = run_validator("validate-instrument-api", expanded_path);
  EXPECT_EQ(ret, 0) << "Validation failed for expanded Keysight instrument API";
}

TEST(SchemaValidatorTest, ValidateQuantumDotDeviceConfig) {
  int ret = run_validator("validate-quantum-dot-config",
                          "examples/one_charge_sensor_quantum_dot_device.yaml");
  EXPECT_EQ(ret, 0) << "Validation failed for quantum dot device config";
}

TEST(SchemaValidatorTest, GenerateAndValidateAgilentInstrumentConfiguration) {
  std::string api_path =
      expand_template("examples/instrument-apis/agi_34401a.yaml");
  auto config_path = generate_configuration(api_path);
  auto ret2 = run_validator("validate-instrument-config", config_path);
  EXPECT_EQ(ret2, 0)
      << "Validation failed for generated Agilent instrument configuration";
}

TEST(SchemaValidatorTest, GenerateAndValidateKeysightInstrumentConfiguration) {
  std::string api_path =
      expand_template("examples/instrument-apis/dso9254a.yaml.tmpl");
  auto config_path = generate_configuration(api_path);
  auto ret2 = run_validator("validate-instrument-config", config_path);
  EXPECT_EQ(ret2, 0)
      << "Validation failed for generated Keysight instrument configuration";
}
