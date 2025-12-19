#include "instrument-server/Logger.hpp"
#include "instrument-server/plugin/PluginRegistry.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <spdlog/common.h>

using namespace instserver::plugin;

TEST(PluginLoadingTest, MockVISAPluginExists) {
  std::vector<std::filesystem::path> search_paths = {
      std::filesystem::current_path() / "tests" / "mock_visa_plugin.so",
      std::filesystem::current_path() / "build" / "tests" /
          "mock_visa_plugin.so",
      std::filesystem::current_path() / "mock_visa_plugin.so"};

  bool found = false;
  for (const auto &path : search_paths) {
    if (std::filesystem::exists(path)) {
      found = true;
      std::cout << "Found mock plugin at: " << path << std::endl;
      break;
    }
  }

  EXPECT_TRUE(found) << "Mock VISA plugin not found in any expected location";
}
