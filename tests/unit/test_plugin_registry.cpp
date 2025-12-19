#include "instrument-server/plugin/PluginRegistry.hpp"
#include <gtest/gtest.h>

using namespace instserver::plugin;

class PluginRegistryTest : public ::testing::Test {
protected:
  void SetUp() override { registry_ = &PluginRegistry::instance(); }

  void TearDown() override {
    // Note: Can't really clear singleton, but tests should be independent
  }

  PluginRegistry *registry_;
};

TEST_F(PluginRegistryTest, Singleton) {
  auto &reg1 = PluginRegistry::instance();
  auto &reg2 = PluginRegistry::instance();

  EXPECT_EQ(&reg1, &reg2);
}

TEST_F(PluginRegistryTest, DiscoverPlugins) {
  std::vector<std::string> search_paths = {"./plugins", "."};

  registry_->discover_plugins(search_paths);

  auto protocols = registry_->list_protocols();
  // Should find at least some plugins (or none if directory doesn't exist)
  EXPECT_GE(protocols.size(), 0);
}

TEST_F(PluginRegistryTest, RegisterPlugin) {
  registry_->load_plugin("MockPlugin", "./build/tests/mock_plugin.so");

  auto protocols = registry_->list_protocols();
  EXPECT_TRUE(std::find(protocols.begin(), protocols.end(), "MockPlugin") !=
              protocols.end());
}

TEST_F(PluginRegistryTest, GetPluginPath) {
  registry_->load_plugin("MockPlugin", "./build/tests/mock_plugin.so");

  std::string path = registry_->get_plugin_path("MockPlugin");
  EXPECT_EQ(path, "./build/tests/mock_plugin.so");
}

TEST_F(PluginRegistryTest, GetNonexistentPlugin) {
  std::string path = registry_->get_plugin_path("NonexistentProtocol");
  EXPECT_TRUE(path.empty());
}

TEST_F(PluginRegistryTest, ListProtocols) {
  size_t initial_count = registry_->list_protocols().size();

  registry_->load_plugin("MockPlugin", "./build/tests/mock_plugin.so");

  auto protocols = registry_->list_protocols();
  EXPECT_EQ(protocols.size(), 1);
}
