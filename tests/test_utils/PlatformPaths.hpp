#pragma once

#include <filesystem>
#include <string>

namespace instserver::test {

/// Get the platform-specific plugin file extension
inline std::string get_plugin_extension() {
#ifdef _WIN32
  return ".dll";
#else
  return ".so";
#endif
}

#ifndef TEST_PLUGIN_DIR
#define TEST_PLUGIN_DIR "."
#endif

inline std::filesystem::path get_test_plugin_dir() { return TEST_PLUGIN_DIR; }

/// Get full path to a test plugin
inline std::filesystem::path
get_test_plugin_path(const std::string &plugin_name) {
  auto dir = get_test_plugin_dir();

#ifdef _WIN32
  return dir / (plugin_name + ".dll");
#else
  return dir / ("lib" + plugin_name + ".so");
#endif
}

} // namespace instserver::test
