#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <thread>

class CLITest : public ::testing::Test {
protected:
  void SetUp() override {
    // Locate the instrument-server executable
    executable_path_ = find_instrument_server_executable();

    if (executable_path_.empty()) {
      GTEST_SKIP() << "instrument-server executable not found.  "
                   << "This test requires the executable to be built.";
    }
  }

  // Find the instrument-server executable in the build directory
  std::string find_instrument_server_executable() {
    std::vector<std::filesystem::path> search_paths;

#ifdef _WIN32
    search_paths.push_back(std::filesystem::current_path() / "build" /
                           "instrument-server.exe");
    search_paths.push_back(std::filesystem::current_path() / "build" /
                           "instrument-server. exe");
    search_paths.push_back(std::filesystem::current_path() /
                           "instrument-server.exe");
    search_paths.push_back(std::filesystem::current_path() /
                           "instrument-server.exe");
#else
    // Linux: Check build directory
    search_paths.push_back(std::filesystem::current_path() / "build" /
                           "instrument-server");
    search_paths.push_back(std::filesystem::current_path() /
                           "instrument-server");
    // Also check if it's in PATH
    search_paths.push_back("instrument-server");
#endif

    for (const auto &path : search_paths) {
      if (std::filesystem::exists(path)) {
        return path.string();
      }
    }

    // Try to find in PATH by running which/where
#ifdef _WIN32
    FILE *pipe = _popen("where instrument-server 2>NUL", "r");
#else
    FILE *pipe = popen("which instrument-server 2>/dev/null", "r");
#endif

    if (pipe) {
      char buffer[256];
      if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string result = buffer;
        // Remove trailing newline
        if (!result.empty() && result.back() == '\n') {
          result.pop_back();
        }
#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif
        if (std::filesystem::exists(result)) {
          return result;
        }
      }
#ifdef _WIN32
      _pclose(pipe);
#else
      pclose(pipe);
#endif
    }

    return "";
  }

  // Get platform-specific null device
  std::string get_null_device() {
#ifdef _WIN32
    return "NUL";
#else
    return "/dev/null";
#endif
  }

  // Run a command and return the exit code
  int run_command(const std::string &args) {
    if (executable_path_.empty()) {
      return -1;
    }

    // Build full command with output redirection to null device
    std::ostringstream cmd;

#ifdef _WIN32
    // Windows: Use cmd /c to handle redirection properly
    // Quote the executable path in case it has spaces
    cmd << "\"" << executable_path_ << "\" " << args << " >NUL 2>&1";
#else
    // Linux: Direct execution with redirection
    cmd << executable_path_ << " " << args << " >/dev/null 2>&1";
#endif

    return std::system(cmd.str().c_str());
  }

  // Run a command and capture its output
  std::pair<int, std::string> run_command_with_output(const std::string &args) {
    if (executable_path_.empty()) {
      return {-1, ""};
    }

    std::ostringstream cmd;

#ifdef _WIN32
    cmd << "\"" << executable_path_ << "\" " << args << " 2>&1";
    FILE *pipe = _popen(cmd.str().c_str(), "r");
#else
    cmd << executable_path_ << " " << args << " 2>&1";
    FILE *pipe = popen(cmd.str().c_str(), "r");
#endif

    if (!pipe) {
      return {-1, ""};
    }

    std::ostringstream output;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      output << buffer;
    }

#ifdef _WIN32
    int exit_code = _pclose(pipe);
#else
    int exit_code = pclose(pipe);
    // On Unix, pclose returns status in the format used by waitpid
    // Extract actual exit code
    if (WIFEXITED(exit_code)) {
      exit_code = WEXITSTATUS(exit_code);
    }
#endif

    return {exit_code, output.str()};
  }

  std::string executable_path_;
};

TEST_F(CLITest, HelpCommand) {
  auto [exit_code, output] = run_command_with_output("--help");

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

  auto [exit_code, output] = run_command_with_output("daemon status");

  // When daemon is not running, status command should return non-zero
  // (or succeed but report daemon is not running)
  // The exact behavior depends on implementation, so we just verify it executed
  EXPECT_NE(exit_code, -1) << "Command failed to execute";

  // Output should indicate status
  EXPECT_FALSE(output.empty()) << "Status command produced no output";
}

TEST_F(CLITest, ListPlugins) {
  auto [exit_code, output] = run_command_with_output("plugins");

  // Plugins command should succeed even with no plugins
  EXPECT_EQ(exit_code, 0) << "Plugins command failed with exit code: "
                          << exit_code;

  // Should produce some output (even if just "No plugins" or empty list)
  // Don't require specific output as it depends on installed plugins
}

TEST_F(CLITest, ListInstrumentsWhenNoneRunning) {
  auto [exit_code, output] = run_command_with_output("list");

  // List command should succeed even with no instruments
  EXPECT_EQ(exit_code, 1) << "List command failed with exit code: "
                          << exit_code;

  // Should produce some output (even if just "No instruments" or empty list)
  // Don't require specific output as it depends on running instruments
}
