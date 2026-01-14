#pragma once

#include "PlatformPaths.hpp"
#include "instrument-server/plugin/PluginRegistry.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"
#include <filesystem>
#include <gtest/gtest.h>

namespace instserver {
namespace test {

class PluginTestFixture : public ::testing::Test {
protected:
  void SetUp() override {
    auto &plugin_reg = plugin::PluginRegistry::instance();

    // Try to load mock_visa_plugin with platform-specific path
    std::filesystem::path plugin_path =
        get_test_plugin_path("mock_visa_plugin");

    if (std::filesystem::exists(plugin_path)) {
      try {
        plugin_reg.load_plugin("VISA", plugin_path.string());
      } catch (const std::exception &e) {
        // Plugin loading failed - tests will skip or fail appropriately
      }
    }
  }

  SyncCoordinator sync_coordinator_;
};

} // namespace test
} // namespace instserver
