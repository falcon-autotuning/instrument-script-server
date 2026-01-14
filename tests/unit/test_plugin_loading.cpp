#include "PlatformPaths.hpp"
#include <filesystem>
#include <gtest/gtest.h>

using namespace instserver::test;

TEST(PluginLoadingTest, MockVISAPluginExists) {
  // Check multiple possible locations
  auto search_paths = get_plugin_search_paths();

  bool found = false;
  std::filesystem::path found_path;

  for (const auto &base_path : search_paths) {
    auto plugin_path =
        base_path / ("mock_visa_plugin" + get_plugin_extension());
    if (std::filesystem::exists(plugin_path)) {
      found = true;
      found_path = plugin_path;
      break;
    }
  }

  EXPECT_TRUE(found) << "Mock VISA plugin not found.  Extension: "
                     << get_plugin_extension() << ", Searched "
                     << search_paths.size() << " paths";

  if (found) {
    EXPECT_TRUE(std::filesystem::is_regular_file(found_path));
    EXPECT_GT(std::filesystem::file_size(found_path), 0);
  }
}
