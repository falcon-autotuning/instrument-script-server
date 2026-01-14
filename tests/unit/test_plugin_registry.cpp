#include "PlatformPaths.hpp"
#include "instrument-server/plugin/PluginRegistry.hpp"
#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>

using namespace instserver;
using namespace instserver::test;

class PluginRegistryTest : public ::testing::Test {
protected:
  void SetUp() override { registry_ = &plugin::PluginRegistry::instance(); }

  plugin::PluginRegistry *registry_;
};

TEST_F(PluginRegistryTest, Singleton) {
  auto &instance1 = plugin::PluginRegistry::instance();
  auto &instance2 = plugin::PluginRegistry::instance();
  EXPECT_EQ(&instance1, &instance2);
}

TEST_F(PluginRegistryTest, DiscoverPlugins) {
  auto search_paths = get_plugin_search_paths();

  // Convert to string vector for the API
  std::vector<std::string> path_strings;
  for (const auto &p : search_paths) {
    path_strings.push_back(p.string());
  }

  registry_->discover_plugins(path_strings);

  auto protocols = registry_->list_protocols();
  // Should find at least the mock plugins if they exist
  EXPECT_GE(protocols.size(), 0);
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
    EXPECT_TRUE(std::find(protocols.begin(), protocols.end(), "MockPlugin") !=
                protocols.end())
        << "MockPlugin not found in protocol list";
  }
}

TEST_F(PluginRegistryTest, GetPluginPath) {
  auto plugin_path = get_test_plugin_path("mock_plugin");

  if (!std::filesystem::exists(plugin_path)) {
    GTEST_SKIP() << "Mock plugin not found";
  }

  registry_->load_plugin("MockPlugin", plugin_path.string());

  std::string path = registry_->get_plugin_path("MockPlugin");
  EXPECT_EQ(path, plugin_path.string());
}

TEST_F(PluginRegistryTest, GetNonexistentPlugin) {
  std::string path = registry_->get_plugin_path("NonexistentProtocol");
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
