#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

// Platform-specific includes for popen/pclose
#ifdef _WIN32
#include <io.h>
#define popen _popen
#define pclose _pclose
#endif
namespace fs = std::filesystem;

// Helper to expand template using the template_expander tool
std::string expand_template(const std::string &tmpl_path) {
  // Create a temp file for the expanded output
  std::string tmp_dir = fs::temp_directory_path().string();
  std::string expanded_path = tmp_dir + "/dso9254a_expanded.yaml";
  std::string cmd = "../template-expander " + tmpl_path + " " + expanded_path;
  int ret = std::system(cmd.c_str());
  if (ret != 0) {
    throw std::runtime_error("Failed to expand template: " + tmpl_path);
  }
  return expanded_path;
}

// Helper to expand template using the template_expander tool
std::string generate_configuration(const std::string &tmpl_path) {
  namespace fs = std::filesystem;
  std::string tmp_dir = fs::temp_directory_path().string();
  std::string expanded_path = tmp_dir + "/dso9254a_config.yaml";
  std::string cmd = "../generate-instrument-config " + tmpl_path + " " +
                    expanded_path + " 2>&1";
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
  std::string cmd = tool + " " + yaml_path;
  return std::system(cmd.c_str());
}

TEST(SchemaValidatorTest, ValidateAgilentInstrumentDirect) {
  fs::path test_dir = fs::path(__FILE__).parent_path();
  fs::path yaml_path =
      test_dir / "../../examples/instrument-apis/agi_34401a.yaml";
  int ret = run_validator("../validate-instrument-api",
                          yaml_path.lexically_normal().string());
  EXPECT_EQ(ret, 0) << "Validation failed for Agilent instrument API";
}

TEST(SchemaValidatorTest, ValidateAgilentInstrumentWithExpander) {
  fs::path test_dir = fs::path(__FILE__).parent_path();
  fs::path yaml_path =
      test_dir / "../../examples/instrument-apis/agi_34401a.yaml";
  std::string expanded_path =
      expand_template(yaml_path.lexically_normal().string());
  int ret = run_validator("../validate-instrument-api", expanded_path);
  EXPECT_EQ(ret, 0) << "Validation failed for expanded Agilent instrument API";
}

TEST(SchemaValidatorTest, ValidateKeysightInstrument) {
  fs::path test_dir = fs::path(__FILE__).parent_path();
  fs::path yaml_path =
      test_dir / "../../examples/instrument-apis/dso9254a.yaml.tmpl";
  std::string expanded_path =
      expand_template(yaml_path.lexically_normal().string());
  int ret = run_validator("../validate-instrument-api", expanded_path);
  EXPECT_EQ(ret, 0) << "Validation failed for expanded Keysight instrument API";
}

TEST(SchemaValidatorTest, ValidateQuantumDotDeviceConfig) {
  fs::path test_dir = fs::path(__FILE__).parent_path();
  fs::path yaml_path =
      test_dir / "../../examples/one_charge_sensor_quantum_dot_device.yaml";
  int ret = run_validator("../validate-quantum-dot-config",
                          yaml_path.lexically_normal().string());
  EXPECT_EQ(ret, 0) << "Validation failed for quantum dot device config";
}

TEST(SchemaValidatorTest, GenerateAndValidateAgilentInstrumentConfiguration) {
  fs::path test_dir = fs::path(__FILE__).parent_path();
  fs::path yaml_path =
      test_dir / "../../examples/instrument-apis/agi_34401a.yaml";
  std::string api_path = expand_template(yaml_path.lexically_normal().string());
  auto config_path = generate_configuration(api_path);
  auto ret2 = run_validator("../validate-instrument-config", config_path);
  EXPECT_EQ(ret2, 0)
      << "Validation failed for generated Agilent instrument configuration";
}

TEST(SchemaValidatorTest, GenerateAndValidateKeysightInstrumentConfiguration) {
  fs::path test_dir = fs::path(__FILE__).parent_path();
  fs::path yaml_path =
      test_dir / "../../examples/instrument-apis/dso9254a.yaml.tmpl";
  std::string api_path = expand_template(yaml_path.lexically_normal().string());
  auto config_path = generate_configuration(api_path);
  auto ret2 = run_validator("../validate-instrument-config", config_path);
  EXPECT_EQ(ret2, 0)
      << "Validation failed for generated Keysight instrument configuration";
}
