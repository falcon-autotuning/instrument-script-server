#pragma once
#include "instrument-server/plugin/PluginRegistry.hpp"

#include <gtest/gtest.h>
namespace instserver {
namespace test {
class PluginTestFixture : public ::testing::Test {
protected:
  plugin::PluginRegistry *plugin_registry_;
  void SetUp() override {
    plugin_registry_ = &plugin::PluginRegistry::instance();
    LoadMockPlugins();
  }
  void LoadMockPlugins() {
    plugin_registry_->load_plugin("VISA", "./build/tests/mock_visa_plugin.so");
  }
};
} // namespace test
} // namespace instserver
