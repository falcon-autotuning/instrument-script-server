#include "instrument-server/plugin/PluginLoader.hpp"
#include "instrument-server/plugin/PluginRegistry.hpp"
#include <gtest/gtest.h>

using namespace instserver::plugin;

TEST(PluginTest, LoadInvalidPath) {
  EXPECT_THROW(
      { PluginLoader loader("/nonexistent/plugin.so"); }, std::exception);
}

TEST(PluginTest, RegistryOperations) {
  auto &registry = PluginRegistry::instance();

  // Initially no plugins
  EXPECT_FALSE(registry.has_plugin("TestProtocol"));

  // Register plugin (will fail with nonexistent path, but tests the API)
  bool loaded = registry.load_plugin("TestProtocol", "./test_plugin.so");
  // Expect failure since plugin doesn't exist
  EXPECT_FALSE(loaded);

  // List protocols
  auto protocols = registry.list_protocols();
  EXPECT_TRUE(std::find(protocols.begin(), protocols.end(), "TestProtocol") ==
              protocols.end());
}
