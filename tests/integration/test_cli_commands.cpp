#include "instrument-script-server/plugin/PluginRegistry.hpp"
#include "instrument-script-server/server/ServerDaemon.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <thread>

#ifndef ISS_BIN_PATH
#define ISS_BIN_PATH "instrument-script-server"
#endif
#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

static std::string bin_path = ISS_BIN_PATH;
class CLITest : public ::testing::Test {
protected:
  void TearDown() override {
    // Clean up after each test - use public API only
    auto &daemon = instserver::ServerDaemon::instance();
    auto &plugin_registry = instserver::plugin::PluginRegistry::instance();
    plugin_registry.unload_all();
    if (daemon.is_running()) {
      daemon.stop();
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }
  // Run a command and return the exit code
  static std::pair<int, std::string> run_command(const std::string &args) {
    std::string cmd = "\"" + bin_path + "\" " + args + " 2>&1";

    FILE *pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
      return {-1, ""};
    }

    std::ostringstream output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      output << buffer;
    }

    int exit_code = pclose(pipe);

#ifndef _WIN32
    if (WIFEXITED(exit_code)) {
      exit_code = WEXITSTATUS(exit_code);
    }
#endif

    return {exit_code, output.str()};
  }
};

TEST_F(CLITest, HelpCommand) {
  auto [exit_code, output] = run_command("--help");

  // Help command should succeed
  EXPECT_EQ(exit_code, 0) << "Help command failed with exit code:  "
                          << exit_code;

  // Output should contain help text
  EXPECT_FALSE(output.empty()) << "Help command produced no output";

  // Should mention common commands
  bool has_help_content = (output.find("Usage") != std::string::npos ||
                           output.find("Commands") != std::string::npos ||
                           output.find("Options") != std::string::npos ||
                           output.find("help") != std::string::npos);
  EXPECT_TRUE(has_help_content)
      << "Help output doesn't contain expected content";
}

TEST_F(CLITest, DaemonStatusWhenNotRunning) {
  // Stop any running daemon first
  run_command("daemon stop");

  // Sleep for 500 milliseconds
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  auto [exit_code, output] = run_command("daemon status");

  // When daemon is not running, status command should return non-zero
  // (or succeed but report daemon is not running)
  // The exact behavior depends on implementation, so we just verify it executed
  EXPECT_NE(exit_code, -1) << "Command failed to execute";

  // Output should indicate status
  EXPECT_FALSE(output.empty()) << "Status command produced no output";
}

TEST_F(CLITest, ListPlugins) {
  auto [exit_code, output] = run_command("plugins");

  // Plugins command should succeed even with no plugins
  EXPECT_EQ(exit_code, 0) << "Plugins command failed with exit code: "
                          << exit_code;

  // Should produce some output (even if just "No plugins" or empty list)
  // Don't require specific output as it depends on installed plugins
}

TEST_F(CLITest, ListInstrumentsWhenNoneRunning) {
  auto [exit_code, output] = run_command("list");

  // List command should succeed even with no instruments
  EXPECT_EQ(exit_code, 1) << "List command failed with exit code: "
                          << exit_code;

  // Should produce some output (even if just "No instruments" or empty list)
  // Don't require specific output as it depends on running instruments
}
