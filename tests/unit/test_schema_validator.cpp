#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <iostream>
#include <stdexcept>
#include <string>

// Platform-specific includes for popen/pclose
#ifdef _WIN32
#include <io.h>
#define popen _popen
#define pclose _pclose
#endif

namespace fs = std::filesystem;

/**
 * @brief Get the directory containing the executables (one level up from test
 * dir)
 */
fs::path get_executables_dir() {
  // Test is in build/release/tests, executables are in build/release
  return fs::current_path().parent_path();
}

/**
 * @brief Find an executable in the build/release directory
 */
fs::path find_executable(const std::string &exe_name) {
  fs::path exe_dir = get_executables_dir();
  fs::path exe_path = exe_dir / exe_name;

#ifdef _WIN32
  if (!exe_path.has_extension()) {
    exe_path += ".exe";
  }
#endif

  if (!fs::exists(exe_path)) {
    throw std::runtime_error("Could not find executable: " + exe_path.string());
  }

  return exe_path;
}

/**
 * @brief Escape backslashes for Windows cmd.exe
 */
std::string escape_for_cmd(const std::string &str) {
#ifdef _WIN32
  std::string result;
  for (char c : str) {
    if (c == '\\') {
      result += "\\\\"; // Double the backslash for cmd.exe
    } else {
      result += c;
    }
  }
  return result;
#else
  return str;
#endif
}

/**
 * @brief Quote a path for use in shell commands (handles spaces and special
 * chars)
 */
std::string quote_path(const fs::path &path) {
#ifdef _WIN32
  // On Windows, escape backslashes and wrap in quotes
  std::string escaped = escape_for_cmd(path.string());
  return "\"" + escaped + "\"";
#else
  // On Linux, escape spaces and special characters
  std::string str = path.string();
  std::string result;
  for (char c : str) {
    if (c == ' ' || c == '&' || c == '|' || c == ';' || c == '(' || c == ')') {
      result += '\\';
    }
    result += c;
  }
  return result;
#endif
}

/**
 * @brief Helper to expand template using the template_expander tool
 */
std::string expand_template(const std::string &tmpl_path) {
  fs::path expander_exe = find_executable("template-expander");
  fs::path tmp_dir = fs::temp_directory_path();
  fs::path expanded_path = tmp_dir / "dso9254a_expanded.yaml";

  std::string cmd = quote_path(expander_exe) + " " + quote_path(tmpl_path) +
                    " " + quote_path(expanded_path);

  int ret = std::system(cmd.c_str());
  if (ret != 0) {
    throw std::runtime_error("Failed to expand template: " + tmpl_path);
  }
  return expanded_path.string();
}

/**
 * @brief Helper to generate configuration using the generate-instrument-config
 * tool
 */
std::string generate_configuration(const std::string &tmpl_path) {
  fs::path generator_exe = find_executable("generate-instrument-config");
  fs::path tmp_dir = fs::temp_directory_path();
  fs::path expanded_path = tmp_dir / "dso9254a_config.yaml";

  std::string cmd = quote_path(generator_exe) + " " + quote_path(tmpl_path) +
                    " " + quote_path(expanded_path) + " 2>&1";

  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    throw std::runtime_error("popen() failed!");
  }

  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    std::cout << buffer; // Forward output to test's stdout
  }

  int ret = pclose(pipe);
  if (ret != 0) {
    throw std::runtime_error("Failed to generate configuration: " +
                             expanded_path.string());
  }

  return expanded_path.string();
}

/**
 * @brief Helper to run a CLI validator tool
 */
int run_validator(const std::string &tool, const std::string &yaml_path) {
  fs::path validator_exe = find_executable(tool);
  std::string cmd = quote_path(validator_exe) + " " + quote_path(yaml_path);
  return std::system(cmd.c_str());
}

// ... rest of your tests remain the same ...
