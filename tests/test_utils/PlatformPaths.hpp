#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace instserver {
namespace test {

/// Get the platform-specific plugin file extension
inline std::string get_plugin_extension() {
#ifdef _WIN32
  return ".dll";
#else
  return ".so";
#endif
}

/// Get the platform-specific plugin directory for tests
inline std::filesystem::path get_test_plugin_dir() {
  return std::filesystem::current_path() / "build" / "tests";
}

/// Get full path to a test plugin
inline std::filesystem::path
get_test_plugin_path(const std::string &plugin_name) {
  return get_test_plugin_dir() / (plugin_name + get_plugin_extension());
}

/// Get alternative plugin search paths (for plugin discovery)
inline std::vector<std::filesystem::path> get_plugin_search_paths() {
  std::vector<std::filesystem::path> paths;

  paths.push_back(std::filesystem::current_path() / "build" / "tests");
  paths.push_back(std::filesystem::current_path() / "build");

  return paths;
}

} // namespace test
} // namespace instserver
