#include "PlatformPaths.hpp"
#include "instrument-server/plugin/PluginInterface.h"
#include "instrument-server/plugin/PluginLoader.hpp"
#include <gtest/gtest.h>

using namespace instserver;
using namespace instserver::test;

class PluginLoaderTest : public ::testing::Test {
protected:
  void SetUp() override {
    plugin_path_ = get_test_plugin_path("mock_plugin");

    if (!std::filesystem::exists(plugin_path_)) {
      skip_tests_ = true;
    }
  }

  std::filesystem::path plugin_path_;
  bool skip_tests_ = false;
};

TEST_F(PluginLoaderTest, LoadValidPlugin) {
  if (skip_tests_) {
    GTEST_SKIP() << "Mock plugin not found at:  " << plugin_path_;
  }

  plugin::PluginLoader loader(plugin_path_.string());
  EXPECT_TRUE(loader.is_loaded());
}

TEST_F(PluginLoaderTest, LoadInvalidPath) {
  // FIXED:  Catch exception that's thrown on Linux when library doesn't exist
  std::string invalid_path = "nonexistent" + get_plugin_extension();

  try {
    plugin::PluginLoader loader(invalid_path);
    // If no exception, check that it's not loaded
    EXPECT_FALSE(loader.is_loaded());
  } catch (const std::exception &e) {
    // Expected behavior on Linux - exception when library not found
    // Test passes - we handled the invalid path correctly
    SUCCEED();
  }
}

TEST_F(PluginLoaderTest, GetMetadata) {
  if (skip_tests_) {
    GTEST_SKIP() << "Mock plugin not found";
  }

  plugin::PluginLoader loader(plugin_path_.string());
  ASSERT_TRUE(loader.is_loaded());

  auto metadata = loader.get_metadata();
  EXPECT_GT(strlen(metadata.name), 0);
  EXPECT_GT(strlen(metadata.version), 0);
}

TEST_F(PluginLoaderTest, Initialize) {
  if (skip_tests_) {
    GTEST_SKIP() << "Mock plugin not found";
  }

  plugin::PluginLoader loader(plugin_path_.string());
  ASSERT_TRUE(loader.is_loaded());

  PluginConfig config{};
  strncpy(config.instrument_name, "TestInstrument", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(config.connection_json, "{\"address\":\"mock://test\"}",
          PLUGIN_MAX_PAYLOAD - 1);
  strncpy(config.api_definition_json, "{}", PLUGIN_MAX_PAYLOAD - 1);

  int result = loader.initialize(config);
  EXPECT_EQ(result, 0);
}

TEST_F(PluginLoaderTest, ExecuteCommand) {
  if (skip_tests_) {
    GTEST_SKIP() << "Mock plugin not found";
  }

  plugin::PluginLoader loader(plugin_path_.string());
  ASSERT_TRUE(loader.is_loaded());

  // Initialize plugin first
  PluginConfig config{};
  strncpy(config.instrument_name, "TestInstrument", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(config.connection_json, "{\"address\": \"mock://test\"}",
          PLUGIN_MAX_PAYLOAD - 1);
  strncpy(config.api_definition_json, "{}", PLUGIN_MAX_PAYLOAD - 1);

  int init_result = loader.initialize(config);
  ASSERT_EQ(init_result, 0) << "Plugin initialization failed";

  // Execute a command - use a command the mock plugin actually supports
  PluginCommand cmd{};
  strncpy(cmd.id, "test_cmd_001", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(cmd.instrument_name, "TestInstrument", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(cmd.verb, "ECHO", PLUGIN_MAX_STRING_LEN - 1); // Use a known command
  cmd.expects_response = true;
  cmd.param_count = 0;

  PluginResponse resp{};
  int result = loader.execute_command(cmd, resp);

  // Command execution should succeed
  EXPECT_EQ(result, 0) << "Command execution failed";
  EXPECT_TRUE(resp.success) << "Command marked as failed in response";
}
