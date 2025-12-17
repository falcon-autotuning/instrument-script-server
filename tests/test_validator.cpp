#include "instrument-server/schema_validator.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>

using namespace instserver;
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

TEST(SchemaValidatorTest, ValidateAgilentInstrumentDirect) {
  // Validate the Agilent instrument directly (not a template)
  auto result = SchemaValidator::validate_instrument_api(
      "examples/instrument-apis/agi_34401a.yaml");
  EXPECT_TRUE(result.valid)
      << "Validation failed:\n"
      << [&result] {
           std::string msg;
           for (const auto &err : result.errors)
             msg += "  - " + err.path + ": " + err.message + "\n";
           return msg;
         }();
}

TEST(SchemaValidatorTest, ValidateAgilentInstrumentWithExpander) {
  // Validate the Agilent instrument after running through the expander (should
  // still work)
  std::string expanded_path =
      expand_template("examples/instrument-apis/agi_34401a.yaml");
  auto result = SchemaValidator::validate_instrument_api(expanded_path);
  EXPECT_TRUE(result.valid)
      << "Validation failed:\n"
      << [&result] {
           std::string msg;
           for (const auto &err : result.errors)
             msg += "  - " + err.path + ": " + err.message + "\n";
           return msg;
         }();
  std::remove(expanded_path.c_str());
}

TEST(SchemaValidatorTest, ValidateKeysightInstrument) {
  std::string expanded_path =
      expand_template("examples/instrument-apis/dso9254a.yaml.tmpl");
  auto result = SchemaValidator::validate_instrument_api(expanded_path);
  EXPECT_TRUE(result.valid)
      << "Validation failed:\n"
      << [&result] {
           std::string msg;
           for (const auto &err : result.errors)
             msg += "  - " + err.path + ": " + err.message + "\n";
           return msg;
         }();
  std::remove(expanded_path.c_str());
}

TEST(SchemaValidatorTest, ParseKeysightInstrument) {
  std::string expanded_path =
      expand_template("examples/instrument-apis/dso9254a.yaml.tmpl");
  EXPECT_NO_THROW({
    auto api = SchemaValidator::parse_instrument_api(expanded_path);
    EXPECT_EQ(api.instrument.vendor, "Keysight");
    EXPECT_EQ(api.instrument.model, "DSO9254A");
    EXPECT_EQ(api.protocol.type, "VISA");
  });
  std::remove(expanded_path.c_str());
}

TEST(SchemaValidatorTest, ValidateSystemContext) {
  auto result =
      SchemaValidator::validate_system_context("examples/system_context.yaml");
  EXPECT_TRUE(result.valid)
      << "Validation failed:\n"
      << [&result] {
           std::string msg;
           for (const auto &err : result.errors)
             msg += "  - " + err.path + ": " + err.message + "\n";
           return msg;
         }();
}

TEST(SchemaValidatorTest, ValidateRuntimeContexts) {
  // If you have a validate_runtime_contexts, use it here.
  // Otherwise, just check that parsing works.
  EXPECT_NO_THROW({
    auto contexts = SchemaValidator::parse_runtime_contexts(
        "examples/runtime_contexts.yaml");
    EXPECT_GT(contexts.size(), 0);
  });
}

TEST(SchemaValidatorTest, ParseRuntimeContexts) {
  EXPECT_NO_THROW({
    auto contexts = SchemaValidator::parse_runtime_contexts(
        "examples/runtime_contexts.yaml");
    EXPECT_GT(contexts.size(), 0);
    for (const auto &[id, ctx] : contexts) {
      EXPECT_FALSE(ctx.name.empty());
    }
  });
}

TEST(SchemaValidatorTest, ValidateQuantumDotDeviceConfig) {
  auto result = SchemaValidator::validate_quantum_dot_device(
      "examples/one_charge_sensor_quantum_dot_device.yaml");
  EXPECT_TRUE(result.valid)
      << "Validation failed:\n"
      << [&result] {
           std::string msg;
           for (const auto &err : result.errors)
             msg += "  - " + err.path + ": " + err.message + "\n";
           return msg;
         }();
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
  auto result = SchemaValidator::validate_instrument_configuration(config_path);
  EXPECT_TRUE(result.valid)
      << "Validation failed:\n"
      << [&result] {
           std::string msg;
           for (const auto &err : result.errors)
             msg += "  - " + err.path + ": " + err.message + "\n";
           return msg;
         }();
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
  auto result = SchemaValidator::validate_instrument_configuration(config_path);
  EXPECT_TRUE(result.valid)
      << "Validation failed:\n"
      << [&result] {
           std::string msg;
           for (const auto &err : result.errors)
             msg += "  - " + err.path + ": " + err.message + "\n";
           return msg;
         }();
  std::remove(config_path.c_str());
}
