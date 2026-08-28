#include "PlatformPaths.hpp"
#include "instrument-script-server/daemon/PluginRegistry.hpp"
#include <algorithm>
#include <gtest/gtest.h>
#include <instrument-log/inst_logging.h>

using namespace instserver;
using namespace instserver::test;

class PluginRegistryTest : public ::testing::Test {
protected:
  void SetUp() override {
    registry_ = &plugin::PluginRegistry::instance();
    // Initialize logger
    log_path_ =
        std::filesystem::temp_directory_path() / "test_main_integration.log";
    inst_log_shutdown();
    inst_log_init(log_path_.string().c_str(), INST_LOG_DEBUG, "instrument",
                  1024 * 1024 * 10, // 10 MB
                  3);               // rotation count
  }

  void TearDown() override {
    registry_->unload_all();
    inst_log_flush();
    inst_log_shutdown();
    std::error_code ec;
    std::filesystem::remove(log_path_, ec);
  }
  plugin::PluginRegistry *registry_;
  std::filesystem::path log_path_;
};

TEST_F(PluginRegistryTest, Singleton) {
  auto &instance1 = plugin::PluginRegistry::instance();
  auto &instance2 = plugin::PluginRegistry::instance();
  EXPECT_EQ(&instance1, &instance2);
}

TEST_F(PluginRegistryTest, DiscoverPlugins) {
  auto plugin_path = get_test_plugin_dir();
  ASSERT_TRUE(std::filesystem::exists(plugin_path))
      << "Plugins not found at: " << plugin_path;

  // Use the directory containing the plugin for discovery
  std::vector<std::string> path_strings = {plugin_path.string()};
  registry_->discover_plugins(path_strings);
  auto protocols = registry_->list_protocols();
  // Should find at least the mock plugin protocol if it exists
  EXPECT_GE(protocols.size(), 1);
}

TEST_F(PluginRegistryTest, RegisterPlugin) {
  auto plugin_path = get_test_plugin_path("mock_plugin");

  if (!std::filesystem::exists(plugin_path)) {
    GTEST_SKIP() << "Mock plugin not found at: " << plugin_path;
  }

  bool loaded = registry_->load_plugin("MockPlugin", plugin_path.string());
  EXPECT_TRUE(loaded) << "Failed to load plugin from: " << plugin_path;

  if (loaded) {
    auto protocols = registry_->list_protocols();
    EXPECT_TRUE(std::ranges::find(protocols, "MockPlugin") != protocols.end())
        << "MockPlugin not found in protocol list";
  }
}

TEST_F(PluginRegistryTest, GetPluginPath) {
  auto plugin_path = get_test_plugin_path("mock_plugin");

  if (!std::filesystem::exists(plugin_path)) {
    GTEST_SKIP() << "Mock plugin not found";
  }

  registry_->load_plugin("MockPlugin", plugin_path.string());

  std::string path = registry_->get_plugin_path("MockPlugin").string();
  EXPECT_EQ(path, plugin_path.string());
}

TEST_F(PluginRegistryTest, GetNonexistentPlugin) {
  std::string path = registry_->get_plugin_path("NonexistentProtocol").string();
  EXPECT_TRUE(path.empty());
}

TEST_F(PluginRegistryTest, ListProtocols) {
  auto plugin_path = get_test_plugin_path("mock_plugin");

  if (!std::filesystem::exists(plugin_path)) {
    GTEST_SKIP() << "Mock plugin not found";
  }

  size_t initial_count = registry_->list_protocols().size();

  registry_->load_plugin("Protocol1", plugin_path.string());
  registry_->load_plugin("Protocol2", plugin_path.string());

  auto protocols = registry_->list_protocols();
  EXPECT_EQ(protocols.size(), initial_count + 2);
}
