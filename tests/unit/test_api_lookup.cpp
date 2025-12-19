#include "instrument-server/Logger.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <spdlog/common.h>
using namespace instserver;

class APILookupTest : public ::testing::Test {
protected:
  void SetUp() override {
    registry_ = &InstrumentRegistry::instance();
    test_data_dir_ = std::filesystem::current_path() / "tests" / "data";
    InstrumentLogger::instance().shutdown();
    auto tmp = std::filesystem::temp_directory_path();
    auto log_path_ = tmp / ("instrument_test.log");
    InstrumentLogger::instance().init(log_path_.string(), spdlog::level::debug);
    if (auto l = spdlog::get("instrument")) {
      l->flush_on(spdlog::level::debug);
    }
  }

  void TearDown() override { registry_->stop_all(); }

  InstrumentRegistry *registry_;
  std::filesystem::path test_data_dir_;
};

TEST_F(APILookupTest, GetInstrumentMetadata) {
  auto config_path = test_data_dir_ / "mock_instrument1.yaml";

  if (!std::filesystem::exists(config_path)) {
    GTEST_SKIP() << "Config not found";
  }

  ASSERT_TRUE(registry_->create_instrument(config_path.string()));

  auto metadata = registry_->get_instrument_metadata("MockInstrument1");
  ASSERT_TRUE(metadata.has_value());
  EXPECT_EQ(metadata->name, "MockInstrument1");
  EXPECT_TRUE(metadata->config.contains("name"));
  EXPECT_TRUE(metadata->api_def.contains("commands"));
  EXPECT_TRUE(metadata->api_def.contains("io"));
}

TEST_F(APILookupTest, CommandExpectsResponse) {
  auto config_path = test_data_dir_ / "mock_instrument. yaml";

  if (!std::filesystem::exists(config_path)) {
    GTEST_SKIP() << "Config not found";
  }

  ASSERT_TRUE(registry_->create_instrument(config_path.string()));

  // Commands with non-empty outputs array expect response
  EXPECT_TRUE(registry_->command_expects_response("MockInstrument1", "IDN"));
  EXPECT_TRUE(registry_->command_expects_response("MockInstrument1", "ECHO"));
  EXPECT_TRUE(
      registry_->command_expects_response("MockInstrument1", "MEASURE"));
  EXPECT_TRUE(registry_->command_expects_response("MockInstrument1", "GET"));
  EXPECT_TRUE(
      registry_->command_expects_response("MockInstrument1", "GET_DOUBLE"));
  EXPECT_TRUE(
      registry_->command_expects_response("MockInstrument1", "GET_STRING"));
  EXPECT_TRUE(
      registry_->command_expects_response("MockInstrument1", "GET_BOOL"));
  EXPECT_TRUE(
      registry_->command_expects_response("MockInstrument1", "GET_ARRAY"));
  EXPECT_TRUE(
      registry_->command_expects_response("MockInstrument1", "GET_RANGE"));

  // Commands with empty outputs array don't expect response
  EXPECT_FALSE(registry_->command_expects_response("MockInstrument1", "SET"));
  EXPECT_FALSE(
      registry_->command_expects_response("MockInstrument1", "SET_RANGE"));
  EXPECT_FALSE(
      registry_->command_expects_response("MockInstrument1", "CONFIGURE"));
  EXPECT_FALSE(registry_->command_expects_response("MockInstrument1", "RESET"));
}

TEST_F(APILookupTest, GetResponseType) {
  auto config_path = test_data_dir_ / "mock_instrument.yaml";

  if (!std::filesystem::exists(config_path)) {
    GTEST_SKIP() << "Config not found";
  }

  ASSERT_TRUE(registry_->create_instrument(config_path.string()));

  // Check response types from io definitions
  auto type_measure =
      registry_->get_response_type("MockInstrument1", "MEASURE");
  ASSERT_TRUE(type_measure.has_value());
  EXPECT_EQ(*type_measure, "float"); // current is float in io

  auto type_idn = registry_->get_response_type("MockInstrument1", "IDN");
  ASSERT_TRUE(type_idn.has_value());
  EXPECT_EQ(*type_idn, "string"); // message is string in io

  auto type_bool = registry_->get_response_type("MockInstrument1", "GET_BOOL");
  ASSERT_TRUE(type_bool.has_value());
  EXPECT_EQ(*type_bool, "boolean"); // status is boolean in io

  auto type_array =
      registry_->get_response_type("MockInstrument1", "GET_ARRAY");
  ASSERT_TRUE(type_array.has_value());
  EXPECT_EQ(*type_array, "array"); // waveform is array in io

  auto type_range =
      registry_->get_response_type("MockInstrument1", "GET_RANGE");
  ASSERT_TRUE(type_range.has_value());
  EXPECT_EQ(*type_range, "float"); // range is float in io

  // Commands without outputs should return nullopt
  auto type_set = registry_->get_response_type("MockInstrument1", "SET");
  EXPECT_FALSE(type_set.has_value());

  auto type_reset = registry_->get_response_type("MockInstrument1", "RESET");
  EXPECT_FALSE(type_reset.has_value());
}

TEST_F(APILookupTest, UnknownCommand) {
  auto config_path = test_data_dir_ / "mock_instrument. yaml";

  if (!std::filesystem::exists(config_path)) {
    GTEST_SKIP() << "Config not found";
  }

  ASSERT_TRUE(registry_->create_instrument(config_path.string()));

  // Unknown command should default to no response expected
  EXPECT_FALSE(
      registry_->command_expects_response("MockInstrument1", "NONEXISTENT"));

  auto type = registry_->get_response_type("MockInstrument1", "NONEXISTENT");
  EXPECT_FALSE(type.has_value());
}

TEST_F(APILookupTest, UnknownInstrument) {
  EXPECT_FALSE(registry_->command_expects_response("NonExistent", "MEASURE"));

  auto metadata = registry_->get_instrument_metadata("NonExistent");
  EXPECT_FALSE(metadata.has_value());
}

TEST_F(APILookupTest, IOOutputsStructure) {
  auto config_path = test_data_dir_ / "mock_instrument.yaml";

  if (!std::filesystem::exists(config_path)) {
    GTEST_SKIP() << "Config not found";
  }

  ASSERT_TRUE(registry_->create_instrument(config_path.string()));

  auto metadata = registry_->get_instrument_metadata("MockInstrument1");
  ASSERT_TRUE(metadata.has_value());

  const auto &api_def = metadata->api_def;
  const auto &commands = api_def["commands"];

  // Verify GET_RANGE has proper structure per schema
  ASSERT_TRUE(commands.contains("GET_RANGE"));
  const auto &get_range = commands["GET_RANGE"];

  ASSERT_TRUE(get_range.contains("outputs"));
  ASSERT_TRUE(get_range["outputs"].is_array());
  ASSERT_FALSE(get_range["outputs"].empty());
  EXPECT_EQ(get_range["outputs"][0], "range");

  // Verify range exists in io
  const auto &io_defs = api_def["io"];
  bool found_range = false;
  for (const auto &io : io_defs) {
    if (io["name"] == "range") {
      found_range = true;
      EXPECT_EQ(io["type"], "float");
      break;
    }
  }
  EXPECT_TRUE(found_range);

  // Verify SET has empty outputs per schema
  ASSERT_TRUE(commands.contains("SET"));
  const auto &set = commands["SET"];
  ASSERT_TRUE(set.contains("outputs"));
  ASSERT_TRUE(set["outputs"].is_array());
  EXPECT_TRUE(set["outputs"].empty());
}
