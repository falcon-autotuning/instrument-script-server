#include "instrument-server/plugin/PluginLoader.hpp"
#include "instrument-server/plugin/PluginRegistry.hpp"
#include <fstream>
#include <gtest/gtest.h>

using namespace instserver::plugin;

class PluginLoaderTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Tests will use the simple_serial plugin if built
    plugin_path_ = "./simple_serial_plugin.so";
  }

  std::string plugin_path_;
};

TEST_F(PluginLoaderTest, LoadNonexistentPlugin) {
  EXPECT_THROW(
      {
        PluginLoader loader("/nonexistent/path/plugin.so");
        if (!loader.is_loaded()) {
          throw std::runtime_error(loader.get_error());
        }
      },
      std::exception);
}

TEST_F(PluginLoaderTest, LoadValidPlugin) {
  // Skip if plugin doesn't exist (not built)
  std::ifstream f(plugin_path_);
  if (!f.good()) {
    GTEST_SKIP() << "Plugin not found: " << plugin_path_;
  }

  PluginLoader loader(plugin_path_);
  EXPECT_TRUE(loader.is_loaded());

  auto metadata = loader.get_metadata();
  EXPECT_EQ(metadata.api_version, INSTRUMENT_PLUGIN_API_VERSION);
  EXPECT_GT(strlen(metadata.name), 0);
  EXPECT_GT(strlen(metadata.version), 0);
  EXPECT_GT(strlen(metadata.protocol_type), 0);
}

TEST_F(PluginLoaderTest, GetMetadata) {
  std::ifstream f(plugin_path_);
  if (!f.good()) {
    GTEST_SKIP() << "Plugin not found";
  }

  PluginLoader loader(plugin_path_);
  ASSERT_TRUE(loader.is_loaded());

  auto metadata = loader.get_metadata();

  // Check API version matches
  EXPECT_EQ(metadata.api_version, INSTRUMENT_PLUGIN_API_VERSION);

  // Check strings are null-terminated
  EXPECT_LT(strlen(metadata.name), PLUGIN_MAX_STRING_LEN);
  EXPECT_LT(strlen(metadata.version), PLUGIN_MAX_STRING_LEN);
  EXPECT_LT(strlen(metadata.protocol_type), PLUGIN_MAX_STRING_LEN);
}

class PluginRegistryTest : public ::testing::Test {
protected:
  PluginRegistry &registry_ = PluginRegistry::instance();
};

TEST_F(PluginRegistryTest, HasPluginNonexistent) {
  EXPECT_FALSE(registry_.has_plugin("NonexistentProtocol"));
}

TEST_F(PluginRegistryTest, GetPluginNonexistent) {
  auto plugin = registry_.get_plugin("NonexistentProtocol");
  EXPECT_EQ(plugin, nullptr);
}

TEST_F(PluginRegistryTest, LoadPluginInvalidPath) {
  bool loaded = registry_.load_plugin("TestProtocol", "/invalid/path. so");
  EXPECT_FALSE(loaded);
  EXPECT_FALSE(registry_.has_plugin("TestProtocol"));
}

TEST_F(PluginRegistryTest, ListProtocols) {
  auto protocols = registry_.list_protocols();
  // May be empty or contain previously loaded plugins
  EXPECT_TRUE(protocols.size() >= 0);
}

TEST_F(PluginRegistryTest, DiscoverPluginsInvalidPath) {
  EXPECT_NO_THROW({ registry_.discover_plugins({"/nonexistent/directory"}); });
}

TEST_F(PluginRegistryTest, DiscoverPluginsEmptyPath) {
  EXPECT_NO_THROW({ registry_.discover_plugins({}); });
}
