#include "instrument-server/plugin/PluginLoader.hpp"
#include <filesystem>
#include <gtest/gtest.h>

using namespace instserver::plugin;

class PluginLoaderTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Find mock plugin
    mock_plugin_path_ =
        std::filesystem::current_path() / "tests" / "mock_plugin.so";

    if (!std::filesystem::exists(mock_plugin_path_)) {
      // Try alternate location
      mock_plugin_path_ = "./build/tests/mock_plugin.so";
    }

    plugin_exists_ = std::filesystem::exists(mock_plugin_path_);
  }

  std::filesystem::path mock_plugin_path_;
  bool plugin_exists_{false};
};

TEST_F(PluginLoaderTest, LoadValidPlugin) {
  if (!plugin_exists_) {
    GTEST_SKIP() << "Mock plugin not found";
  }

  PluginLoader loader(mock_plugin_path_.string());
  EXPECT_TRUE(loader.is_loaded());
}

TEST_F(PluginLoaderTest, LoadInvalidPath) {
  EXPECT_THROW(
      { PluginLoader loader("/nonexistent/plugin.so"); }, std::exception);
}

TEST_F(PluginLoaderTest, GetMetadata) {
  if (!plugin_exists_) {
    GTEST_SKIP() << "Mock plugin not found";
  }

  PluginLoader loader(mock_plugin_path_.string());
  ASSERT_TRUE(loader.is_loaded());

  auto metadata = loader.get_metadata();
  EXPECT_FALSE(std::string(metadata.name).empty());
  EXPECT_FALSE(std::string(metadata.protocol_type).empty());
  EXPECT_EQ(metadata.api_version, INSTRUMENT_PLUGIN_API_VERSION);
}

TEST_F(PluginLoaderTest, Initialize) {
  if (!plugin_exists_) {
    GTEST_SKIP() << "Mock plugin not found";
  }

  PluginLoader loader(mock_plugin_path_.string());
  ASSERT_TRUE(loader.is_loaded());

  PluginConfig config = {};
  strncpy(config.instrument_name, "TestInst", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(config.connection_json, "{}", PLUGIN_MAX_STRING_LEN - 1);

  int result = loader.initialize(config);
  EXPECT_EQ(result, 0);

  loader.shutdown();
}

TEST_F(PluginLoaderTest, ExecuteCommand) {
  if (!plugin_exists_) {
    GTEST_SKIP() << "Mock plugin not found";
  }

  PluginLoader loader(mock_plugin_path_.string());
  ASSERT_TRUE(loader.is_loaded());

  PluginConfig config = {};
  strncpy(config.instrument_name, "TestInst", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(config.connection_json, "{}", PLUGIN_MAX_STRING_LEN - 1);
  loader.initialize(config);

  PluginCommand cmd = {};
  strncpy(cmd.id, "test-1", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(cmd.instrument_name, "TestInst", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(cmd.verb, "IDN", PLUGIN_MAX_STRING_LEN - 1);
  cmd.expects_response = true;

  PluginResponse resp = {};

  int result = loader.execute_command(cmd, resp);
  EXPECT_EQ(result, 0);
  EXPECT_TRUE(resp.success);

  loader.shutdown();
}
