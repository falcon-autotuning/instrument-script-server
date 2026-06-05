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
  return std::filesystem::current_path();
}

/// Get full path to a test plugin
inline std::filesystem::path
get_test_plugin_path(const std::string &plugin_name) {

#ifdef __linux__
  std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe");
  auto dir = exe.parent_path();
#else
  auto dir = std::filesystem::current_path();
#endif

  return dir / (plugin_name + get_plugin_extension());
}

} // namespace test
} // namespace instserver
