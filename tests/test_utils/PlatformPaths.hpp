#pragma once

#include <filesystem>
#include <iostream>
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

/// Get the platform-specific plugin directory for tests
inline std::filesystem::path get_test_plugin_dir() {
  std::cout << "CWD = " << std::filesystem::current_path() << '\n';
  return std::filesystem::current_path();
}

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
