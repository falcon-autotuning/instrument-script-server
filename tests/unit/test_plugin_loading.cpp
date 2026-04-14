#include "PlatformPaths.hpp"
#include <filesystem>
#include <gtest/gtest.h>

using namespace instserver::test;

TEST(PluginLoadingTest, MockVISAPluginExists) {
  auto plugin_path = get_test_plugin_path("mock_visa_plugin");
  ASSERT_TRUE(std::filesystem::exists(plugin_path))
      << "Mock VISA plugin not found at: " << plugin_path;
  ASSERT_TRUE(std::filesystem::is_regular_file(plugin_path))
      << "Mock VISA plugin is not a regular file: " << plugin_path;
  ASSERT_GT(std::filesystem::file_size(plugin_path), 0)
      << "Mock VISA plugin file is empty: " << plugin_path;
}
