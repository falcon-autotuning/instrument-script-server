#include "instrument-script-server/plugin/PluginRegistry.hpp"
#include <bits/chrono.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
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
using namespace std::chrono_literals;
namespace {
std::string get_runtime_dir() {
  const char *forced = getenv("INSTRUMENT_SERVER_RUNTIME_DIR");
#ifdef _WIN32
  if (forced != nullptr) {
    return forced;
  }
  char *appdata = getenv("LOCALAPPDATA");
  if (appdata != nullptr) {
    return std::string(appdata) + "\\InstrumentServer";
  }
  return ".\\instrument-script-server-runtime";
#else
  if (forced != nullptr) {
    return forced;
  }
  char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
  if (xdg_runtime != nullptr) {
    return std::string(xdg_runtime) + "/instrument-script-server";
  }
  return "/tmp/instrument-script-server-" +
         std::string((getenv("USER") != nullptr) ? getenv("USER") : "unknown");
#endif
}
int get_pid_from_file(const std::string &path) {
  std::ifstream ifs(path);
  int pid = 0;
  if (!(ifs >> pid)) {
    return -1;
  }
  return pid;
}

bool process_alive(int pid) {
#ifdef _WIN32
  HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
  if (!h)
    return false;

  DWORD code;
  GetExitCodeProcess(h, &code);
  CloseHandle(h);
  return code == STILL_ACTIVE;
#else
  return (kill(pid, 0) == 0);
#endif
}
int extract_pid(std::string input) {
  try {
    nlohmann::json json = nlohmann::json::parse(input);
    // Flat format
    if (json.contains("pid") && !json["pid"].is_null()) {
      return json["pid"];
    }
    // CLIOutput JSON format: {"output": [{"pid": x, ...}]}
    if (json.contains("output") && json["output"].is_array() &&
        !json["output"].empty()) {
      const auto &first = json["output"][0];
      if (first.contains("pid") && !first["pid"].is_null()) {
        return first["pid"];
      }
    }
  } catch (...) {
  }
  return -1;
}
bool extract_running(std::string input) {
  try {
    nlohmann::json json = nlohmann::json::parse(input);
    // Flat format
    if (json.contains("running") && !json["running"].is_null()) {
      if (json["running"].is_boolean()) {
        return json["running"].get<bool>();
      }
      if (json["running"].is_number()) {
        return json["running"].get<int>() != 0;
      }
    }
    // CLIOutput JSON format: {"output": [{"running": true, ...}]}
    if (json.contains("output") && json["output"].is_array() &&
        !json["output"].empty()) {
      const auto &first = json["output"][0];
      if (first.contains("running") && !first["running"].is_null()) {
        if (first["running"].is_boolean()) {
          return first["running"].get<bool>();
        }
        if (first["running"].is_number()) {
          return first["running"].get<int>() != 0;
        }
      }
    }
  } catch (...) {
  }
  return false;
}
} // namespace

static std::string bin_path = ISS_BIN_PATH;
class CLITest : public ::testing::Test {
protected:
  void SetUp() override {
    // ensure stopped beforehand
    std::system((bin_path + " daemon stop").c_str());
    std::string pid_file = get_runtime_dir() + "/server.pid";
    for (int i = 0; i < 30; ++i) {
      int pid = get_pid_from_file(pid_file);
      if (pid <= 0 || !process_alive(pid)) {
        break;
      }
      std::this_thread::sleep_for(100ms);
    }
  }
  void TearDown() override {
    // Clean up after each test - use public API only
    auto &plugin_registry = instserver::plugin::PluginRegistry::instance();
    plugin_registry.unload_all();
    std::system((bin_path + " daemon stop").c_str());
    std::string pid_file = get_runtime_dir() + "/server.pid";
    for (int i = 0; i < 30; ++i) {
      int pid = get_pid_from_file(pid_file);
      if (pid <= 0 || !process_alive(pid)) {
        break;
      }
      std::this_thread::sleep_for(100ms);
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
    auto [code, output] =
        run_command("pgrep -f 'instrument-script-server-daemon' | grep -v " +
                    std::to_string(getpid()));
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
  EXPECT_NE(exit_code, 0) << "Command failed to execute";
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
  EXPECT_EQ(exit_code, 0) << "Command failed to execute";

  // Output should indicate status
  EXPECT_FALSE(output.empty()) << "Start command produced no output";

  // Should contain instument started
  bool started = output.find("daemon started") != std::string::npos;

  EXPECT_TRUE(started) << "Daemon was not started with an output message: "
                       << output;

  int pid = extract_pid(output);

  run_iss("daemon stop");
  // Give processes time to shut down
  bool alive = true;
  for (int i = 0; i < 30; ++i) {
    if (pid <= 0 || !process_alive(pid)) {
      alive = false;
      break;
    }
    std::this_thread::sleep_for(100ms);
  }

  // Verify daemon process is gone
  EXPECT_FALSE(alive) << "Daemon process is still alive: " << pid;
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
  int pid = -1;
  {
    auto [exit_code, output] = run_iss("daemon start --json");
    EXPECT_EQ(exit_code, 0) << "daemon start failed to execute";
    pid = extract_pid(output);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // Start an instrument
  {
    auto [exit_code, output] = run_iss(
        "start ./data/mock_instrument1.yaml --plugin ./libmock_visa_plugin.so");
    EXPECT_EQ(exit_code, 0) << "instrument start failed to execute";
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // Check that the instrument is still running
  {
    auto [exit_code, output] = run_iss("status MockInstrument1");
    EXPECT_EQ(exit_code, 0) << "instrument status failed to execute";
    bool running = (output.find("RUNNING") != std::string::npos);
    EXPECT_TRUE(running) << "The MockInstrument1 has stopped running: "
                         << output;
  }
  // Shutdown the instrument
  {
    auto [exit_code, output] = run_iss("stop MockInstrument1");
    EXPECT_EQ(exit_code, 0) << "instrument stop failed to execute";
    bool stopped = (output.find("Stopped instrument") != std::string::npos);
    EXPECT_TRUE(stopped) << "The MockInstrument1 has failed to stop: "
                         << output;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // Shutdown the daemon process
  run_iss("daemon stop");
  bool alive = true;
  for (int i = 0; i < 30; ++i) {
    if (pid <= 0 || !process_alive(pid)) {
      alive = false;
      break;
    }
    std::this_thread::sleep_for(100ms);
  }
  EXPECT_FALSE(alive) << "Daemon process is still alive: " << pid;
}

TEST_F(CLITest, StartInstruments) {
  // Start the background daemon process for the ISS
  int pid = -1;
  {
    auto [exit_code, output] = run_iss("daemon start --json");
    EXPECT_EQ(exit_code, 0) << "daemon start failed to execute";
    pid = extract_pid(output);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // Start an instrument
  {
    auto [exit_code, output] = run_iss(
        "start ./data/mock_instrument1.yaml --plugin ./libmock_visa_plugin.so");
    EXPECT_EQ(exit_code, 0) << "instrument start failed to execute";
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // Check that the instrument is still running
  {
    auto [exit_code, output] = run_iss("status MockInstrument1");
    EXPECT_EQ(exit_code, 0) << "instrument status failed to execute";
    bool running = (output.find("RUNNING") != std::string::npos);
    EXPECT_TRUE(running) << "The MockInstrument1 has stopped running: "
                         << output;
  }
  // Start another instrument
  {
    auto [exit_code, output] = run_iss(
        "start ./data/mock_instrument2.yaml --plugin ./libmock_visa_plugin.so");
    EXPECT_EQ(exit_code, 0) << "instrument start failed to execute";
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // Check that the instrument is still running
  {
    auto [exit_code, output] = run_iss("status MockInstrument2");
    EXPECT_EQ(exit_code, 0) << "instrument status failed to execute";
    bool running = (output.find("RUNNING") != std::string::npos);
    EXPECT_TRUE(running) << "The MockInstrument2 has stopped running: "
                         << output;
  }
  // Shutdown the first instrument
  {
    auto [exit_code, output] = run_iss("stop MockInstrument1");
    EXPECT_EQ(exit_code, 0) << "instrument stop failed to execute";
    bool stopped = (output.find("Stopped instrument") != std::string::npos);
    EXPECT_TRUE(stopped) << "The MockInstrument1 has failed to stop: "
                         << output;
  }
  // Shutdown the second instrument
  {
    auto [exit_code, output] = run_iss("stop MockInstrument2");
    EXPECT_EQ(exit_code, 0) << "instrument stop failed to execute";
    bool stopped = (output.find("Stopped instrument") != std::string::npos);
    EXPECT_TRUE(stopped) << "The MockInstrument2 has failed to stop: "
                         << output;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  // Shutdown the daemon process
  run_iss("daemon stop");
  bool alive = true;
  for (int i = 0; i < 30; ++i) {
    if (pid <= 0 || !process_alive(pid)) {
      alive = false;
      break;
    }
    std::this_thread::sleep_for(100ms);
  }
  EXPECT_FALSE(alive) << "Daemon process is still alive: " << pid;
}

// TODO: Need to perform a measurement directly from the CLI on a running
// instrument and check the output

TEST_F(CLITest, StartCreatesProcessAndPidFile) {
  auto [start_exit, start_out] = run_iss("daemon start --json");
  int pid = -1;
  std::this_thread::sleep_for(200ms);
  auto [exit_code, output] = run_iss("daemon status --json");
  std::cout << "The daemon status is " << output << "\n";
  pid = extract_pid(output);
  bool running = extract_running(output);
  EXPECT_TRUE(running);

  EXPECT_GT(pid, 0);
  EXPECT_TRUE(process_alive(pid));
  run_iss("daemon stop");
  for (int i = 0; i < 30; ++i) {
    if (!process_alive(pid)) {
      break;
    }
    std::this_thread::sleep_for(100ms);
  }
  EXPECT_FALSE(process_alive(pid));
}

TEST_F(CLITest, RestartWorks) {
  auto [exit_code1, output1] = run_iss("daemon start --json");
  std::this_thread::sleep_for(200ms);

  auto [status_code1, status_out1] = run_iss("daemon status --json");
  int pid1 = extract_pid(status_out1);
  ASSERT_GT(pid1, 0);

  run_iss("daemon stop");
  std::this_thread::sleep_for(300ms);

  auto [exit_code2, output2] = run_iss("daemon start --json");
  std::this_thread::sleep_for(200ms);

  auto [status_code2, status_out2] = run_iss("daemon status --json");
  int pid2 = extract_pid(status_out2);
  ASSERT_GT(pid2, 0);

  EXPECT_NE(pid1, pid2);

  run_iss("daemon stop");
}

TEST_F(CLITest, MultipleStartsDoNotDuplicate) {
  auto [exit_code1, output1] = run_iss("daemon start --json");
  std::this_thread::sleep_for(200ms);
  auto [status_code1, status_out1] = run_iss("daemon status --json");
  int pid1 = extract_pid(status_out1);
  ASSERT_GT(pid1, 0);

  run_iss("daemon start --json");
  auto [status_code2, status_out2] = run_iss("daemon status --json");
  int pid2 = extract_pid(status_out2);
  ASSERT_GT(pid2, 0);
  std::this_thread::sleep_for(200ms);

  EXPECT_EQ(pid1, pid2); // same daemon
  run_iss("daemon stop");
}
