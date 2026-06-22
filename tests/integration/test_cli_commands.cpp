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
    std::string cmd = args + " 2>&1";

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
  static std::pair<int, std::string> run_iss(const std::string &args) {
    return run_command("\"" + bin_path + "\" " + args);
  }
  static std::string has_instrument_processes() {
#ifdef _WIN32
    auto [code, output] =
        run_command("tasklist | findstr /i instrument-script-server");
#else
    auto [code, output] = run_command("pgrep -f instrument-script-server");
#endif

    // No output = no processes
    return output;
  }
};

TEST_F(CLITest, HelpCommand) {
  auto [exit_code, output] = run_iss("--help");

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

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  auto [exit_code, output] = run_iss("daemon status");

  // When daemon is not running, status command should return non-zero
  // (or succeed but report daemon is not running)
  // The exact behavior depends on implementation, so we just verify it executed
  EXPECT_NE(exit_code, -1) << "Command failed to execute";

  // Output should indicate status
  EXPECT_FALSE(output.empty()) << "Status command produced no output";
}

TEST_F(CLITest, DaemonStartStop) {
  // Stop any running daemon first
  run_iss("daemon stop");

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  auto [exit_code, output] = run_iss("daemon start --json");

  // When daemon is not running, start command should return non-zero
  // The json should print all the information as a json blob
  // The exact behavior depends on implementation, so we just verify it executed
  EXPECT_NE(exit_code, -1) << "Command failed to execute";

  // Output should indicate status
  EXPECT_FALSE(output.empty()) << "Start command produced no output";

  // Should contain instument started
  bool started = output.find("daemon started") != std::string::npos;

  EXPECT_TRUE(started) << "Daemon was not started with an output message: "
                       << output;

  run_iss("daemon stop");
  // Give processes time to shut down
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Verify no instrument processes remain
  std::string process_check = has_instrument_processes();
  EXPECT_TRUE(process_check.empty())
      << "Instrument processes still running! " << process_check;
}

TEST_F(CLITest, ListPlugins) {
  auto [exit_code, output] = run_iss("discover");

  // Plugins command should succeed even with no plugins
  EXPECT_EQ(exit_code, 0) << "Plugins command failed with exit code: "
                          << exit_code;

  // Should produce some output (even if just "No plugins" or empty list)
  // Don't require specific output as it depends on installed plugins
}

TEST_F(CLITest, ListInstrumentsWhenNoneRunning) {
  auto [exit_code, output] = run_iss("list");

  // List command should succeed even with no instruments
  EXPECT_EQ(exit_code, 1) << "List command failed with exit code: "
                          << exit_code;

  // Should produce some output (even if just "No instruments" or empty list)
  // Don't require specific output as it depends on running instruments
}

TEST_F(CLITest, StartInstrument) {
  // Start the background daemon process for the ISS
  {
    auto [exit_code, output] = run_iss("daemon start --json");
    EXPECT_NE(exit_code, -1) << "daemon start failed to execute";
    bool started = output.find("daemon started") != std::string::npos;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // Start an instrument
  {
    auto [exit_code, output] = run_iss(
        "start ./data/mock_instrument1.yaml --plugin ./mock_visa_plugin.so");
    EXPECT_NE(exit_code, -1) << "instrument start failed to execute";
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // Check that the instrument is still running
  {
    auto [exit_code, output] = run_iss("status MockInstrument1");
    EXPECT_NE(exit_code, -1) << "instrument status failed to execute";
    bool running = (output.find("RUNNING") != std::string::npos);
    EXPECT_TRUE(running) << "The MockInstrument1 has stopped running: "
                         << output;
  }
  // Shutdown the instrument
  {
    auto [exit_code, output] = run_iss("stop MockInstrument1");
    EXPECT_NE(exit_code, -1) << "instrument stop failed to execute";
    bool stopped = (output.find("Stopped instrument") != std::string::npos);
    EXPECT_TRUE(stopped) << "The MockInstrument1 has failed to stop: "
                         << output;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // Shutdown the daemon process
  run_iss("daemon stop");
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  std::string process_check = has_instrument_processes();
  EXPECT_TRUE(process_check.empty())
      << "Instrument processes still running! " << process_check;
}

// TODO: Need to test starting two instruments and shutting them down and that
// they persist
// TODO: Need to perform a measurement directly from the CLI on a running
// instrument and check the output
