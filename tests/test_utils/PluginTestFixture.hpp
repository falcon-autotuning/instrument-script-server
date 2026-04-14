#pragma once

#include "PlatformPaths.hpp"
#include "instrument-script-server/plugin/PluginRegistry.hpp"
#include "instrument-script-server/server/SyncCoordinator.hpp"
#include <filesystem>
#include <gtest/gtest.h>

namespace instserver {
namespace test {

class PluginTestFixture : public ::testing::Test {
protected:
  void SetUp() override {
    auto &plugin_reg = plugin::PluginRegistry::instance();
    std::filesystem::path plugin_path =
        get_test_plugin_path("mock_visa_plugin");
    if (std::filesystem::exists(plugin_path)) {
      try {
        plugin_reg.load_plugin("VISA", plugin_path.string());
      } catch (const std::exception &e) {
        std::cerr << "Failed to load VISA plugin from " << plugin_path << ": "
                  << e.what() << std::endl;
        GTEST_FAIL() << "Failed to load VISA plugin: " << e.what();
      }
    } else {
      std::cerr << "VISA plugin not found at " << plugin_path << std::endl;
      GTEST_FAIL() << "VISA plugin not found at: " << plugin_path;
    }
  }

  SyncCoordinator sync_coordinator_;
};

} // namespace test
} // namespace instserver
