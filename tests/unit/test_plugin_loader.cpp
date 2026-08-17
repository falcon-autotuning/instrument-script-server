#include "PlatformPaths.hpp"
#include "instrument-script-server/core/ErrorCodes.hpp"
#include "instrument-script-server/core/PluginLoader.hpp"
#include "instrument-script-server/daemon/PluginRegistry.hpp"
#include <gtest/gtest.h>
#include <instrument-plugin.h>
#include <plugin-host.h>

using namespace instserver;
using namespace instserver::test;

class PluginLoaderTest : public ::testing::Test {
protected:
  void SetUp() override {
    plugin_path_ = get_test_plugin_path("mock_visa_plugin");

    if (!std::filesystem::exists(plugin_path_)) {
      skip_tests_ = true;
    }
  }
  void TearDown() override {
    auto &registry = instserver::plugin::PluginRegistry::instance();
    registry.unload_all();
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

  PluginConfig config{}; // ✅ stack allocation

  strncpy(config.instrument_name, "TestInstrument", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(config.address, "mock://test", PLUGIN_MAX_STRING_LEN - 1);
  config.baud_rate = 0;
  strncpy(config.custom, "", PLUGIN_MAX_STRING_LEN - 1);

  ErrorCode result = loader.initialize(&config); // ✅ pass pointer
  EXPECT_EQ(result, ErrorCode::NONE);
}

TEST_F(PluginLoaderTest, ExecuteCommand) {
  if (skip_tests_) {
    GTEST_SKIP() << "Mock plugin not found";
  }

  plugin::PluginLoader loader(plugin_path_.string());
  ASSERT_TRUE(loader.is_loaded());

  // Initialize plugin first
  PluginConfig config{}; // ✅ stack allocation

  strncpy(config.instrument_name, "TestInstrument", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(config.address, "mock://test", PLUGIN_MAX_STRING_LEN - 1);
  config.baud_rate = 0;
  strncpy(config.custom, "", PLUGIN_MAX_STRING_LEN - 1);

  ErrorCode init_result = loader.initialize(&config); // ✅ pass pointer
  ASSERT_EQ(init_result, ErrorCode::NONE) << "Plugin initialization failed";

  // Execute a command - use a command the mock plugin actually supports
  PluginCommand cmd{};

  strncpy(cmd.id, "test_cmd_001", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(cmd.command, "ECHO", PLUGIN_MAX_STRING_LEN - 1);
  cmd.is_query = true;
  cmd.timeout_ms = 10;
  cmd.params = param_storage_create_with_capacity(0);

  PluginResponse *resp = plugin_response_create_with_capacity(0);

  ErrorCode result = loader.execute_command(&cmd, resp);

  // Command execution should succeed
  EXPECT_EQ(result, ErrorCode::NONE) << "Command execution failed";
  param_storage_free(cmd.params);
  plugin_response_free(resp);
}
