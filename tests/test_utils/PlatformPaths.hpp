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
#ifdef _WIN32
  // Visual Studio multi-config:  build/tests/Release/
  auto base = std::filesystem::current_path() / "build" / "tests" / "Release";
  if (std::filesystem::exists(base)) {
    return base;
  }
  // Fallback to Debug
  return std::filesystem::current_path() / "build" / "tests" / "Debug";
#else
  // Unix Makefiles: build/tests/
  return std::filesystem::current_path() / "build" / "tests";
#endif
}

/// Get full path to a test plugin
inline std::filesystem::path
get_test_plugin_path(const std::string &plugin_name) {
  return get_test_plugin_dir() / (plugin_name + get_plugin_extension());
}

/// Get alternative plugin search paths (for plugin discovery)
inline std::vector<std::filesystem::path> get_plugin_search_paths() {
  std::vector<std::filesystem::path> paths;

#ifdef _WIN32
  paths.push_back(std::filesystem::current_path() / "build" / "tests" /
                  "Release");
  paths.push_back(std::filesystem::current_path() / "build" / "tests" /
                  "Debug");
  paths.push_back(std::filesystem::current_path() / "build" / "Release");
  paths.push_back(std::filesystem::current_path() / "build" / "Debug");
#else
  paths.push_back(std::filesystem::current_path() / "build" / "tests");
  paths.push_back(std::filesystem::current_path() / "build");
#endif

  return paths;
}

} // namespace test
} // namespace instserver
