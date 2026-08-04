#pragma once

#include "PlatformPaths.hpp"
#include "instrument-script-server/daemon/PluginRegistry.hpp"
#include "instrument-script-server/daemon/SyncCoordinator.hpp"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

namespace instserver::test {
struct LogMatch {
  size_t line_number;
  std::string line;
};

struct LogContents {
  std::string contents;

private:
  static std::vector<LogMatch> find_matches(const std::string &contents,
                                            const std::string &substr) {
    std::vector<LogMatch> matches;

    std::istringstream stream(contents);
    std::string line;
    size_t line_number = 1;

    while (std::getline(stream, line)) {
      if (line.find(substr) != std::string::npos) {
        matches.push_back({.line_number = line_number, .line = line});
      }
      ++line_number;
    }

    return matches;
  }

public:
  void contains(const std::string &substr) const {
    auto matches = find_matches(contents, substr);

    EXPECT_FALSE(matches.empty()) << "Log did not contain: " << substr;
  }

  void does_not_contain(const std::string &substr) const {
    auto matches = find_matches(contents, substr);

    if (!matches.empty()) {
      std::ostringstream msg;
      msg << "Log unexpectedly contained: " << substr << "\n";
      msg << "Matching line(s):\n";

      for (const auto &match : matches) {
        msg << "  line " << match.line_number << ": " << match.line << '\n';
      }

      ADD_FAILURE() << msg.str();
    }
  }

  void contains_error() const { contains("[error]"); }

  void does_not_contain_error() const { does_not_contain("[error]"); }
};

inline LogContents read_log(const std::filesystem::path &path) {
  if (auto l = spdlog::get("instrument")) {
    l->flush();
  }

  // Small delay to allow file system to catch up
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  std::ifstream ifs(path, std::ios::in | std::ios::binary);
  if (!ifs) {
    return {};
  }

  return {std::string((std::istreambuf_iterator<char>(ifs)),
                      (std::istreambuf_iterator<char>()))};
}
template <size_t N>
inline void clear_test_logs(std::array<std::filesystem::path, N> logs) {
  for (const auto &log : logs) {
    std::ofstream(log, std::ios::out | std::ios::trunc);
  }
}

class PluginTestFixture : public ::testing::Test {
protected:
  void SetUp() override {
    auto &plugin_reg = plugin::PluginRegistry::instance();
    std::filesystem::path plugin_path =
        get_test_plugin_path("mock_visa_plugin");
    if (std::filesystem::exists(plugin_path)) {
      try {
        plugin_reg.load_plugin("VISA", plugin_path.generic_string());
      } catch (const std::exception &e) {
        std::cerr << "Failed to load VISA plugin from " << plugin_path << ": "
                  << e.what() << '\n';
        GTEST_FAIL() << "Failed to load VISA plugin: " << e.what();
      }
    } else {
      std::cerr << "VISA plugin not found at " << plugin_path << '\n';
      GTEST_FAIL() << "VISA plugin not found at: " << plugin_path;
    }
  }

  SyncCoordinator sync_coordinator_;
};

} // namespace instserver::test
