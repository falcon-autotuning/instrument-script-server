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
    plugin_path_ = get_test_plugin_path("mock_plugin");

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
  strncpy(config.address, "mock://test", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(config.custom, "", PLUGIN_MAX_STRING_LEN - 1);

  ErrorCode result = loader.initialize(&config);
  EXPECT_EQ(result, ErrorCode::NONE);
}

TEST_F(PluginLoaderTest, InitializeOptionals) {
  if (skip_tests_) {
    GTEST_SKIP() << "Mock plugin not found";
  }

  plugin::PluginLoader loader(plugin_path_.string());
  ASSERT_TRUE(loader.is_loaded());

  PluginConfig config{};

  strncpy(config.instrument_name, "TestInstrument", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(config.address, "mock://test", PLUGIN_MAX_STRING_LEN - 1);
  config.baud_rate = 0;
  strncpy(config.custom, "", PLUGIN_MAX_STRING_LEN - 1);
  config.startup_delay = 10;
  strncpy(config.init_commands[0], "hello", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(config.init_commands[1], "world", PLUGIN_MAX_STRING_LEN - 1);
  for (size_t i = 2; i < STARTUP_COMMANDS; i++) {
    strncpy(config.init_commands[i], "\0", PLUGIN_MAX_STRING_LEN - 1);
  }

  ErrorCode result = loader.initialize(&config);
  EXPECT_EQ(result, ErrorCode::NONE);
}

TEST_F(PluginLoaderTest, ExecuteCommand) {
  if (skip_tests_) {
    GTEST_SKIP() << "Mock plugin not found";
  }

  plugin::PluginLoader loader(plugin_path_.string());
  ASSERT_TRUE(loader.is_loaded());

  PluginConfig config{};

  strncpy(config.instrument_name, "TestInstrument", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(config.address, "mock://test", PLUGIN_MAX_STRING_LEN - 1);
  config.baud_rate = 0;
  strncpy(config.custom, "", PLUGIN_MAX_STRING_LEN - 1);

  ErrorCode init_result = loader.initialize(&config);
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
