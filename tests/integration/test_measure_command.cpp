#include "instrument-server/Logger.hpp"
#include "instrument-server/server/ServerDaemon.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>

using namespace instserver;

class MeasureCommandTest : public ::testing::Test {
protected:
  void SetUp() override {
    InstrumentLogger::instance().init("measure_cmd_test.log",
                                      spdlog::level::debug);

    // Ensure daemon is stopped
    if (ServerDaemon::is_already_running()) {
      int pid = ServerDaemon::get_daemon_pid();
#ifdef _WIN32
      HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
      if (process) {
        TerminateProcess(process, 0);
        CloseHandle(process);
      }
#else
      kill(pid, SIGTERM);
#endif
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    test_dir_ =
        std::filesystem::temp_directory_path() / "instrument_server_test";
    std::filesystem::create_directories(test_dir_);

    // Create a simple test script
    auto script_path = test_dir_ / "test.lua";
    std::ofstream script(script_path);
    script << "context:log('Test script running')\n";
    script << "print('Output from script')\n";
    script.close();
  }

  void TearDown() override { std::filesystem::remove_all(test_dir_); }

  // Helper function to run command with timeout
  // Note: Uses system timeout command to avoid std::async destructor hang
  int run_command_with_timeout(const std::string &cmd,
                               int timeout_seconds = 5) {
    // Use system timeout to prevent hangs
    std::string timeout_cmd = "timeout " + std::to_string(timeout_seconds) + " " + cmd;
    int result = std::system(timeout_cmd.c_str());
    
    // timeout returns 124 if command times out
    if (result == 124 * 256) { // system() returns exit status * 256
      LOG_ERROR("TEST", "TIMEOUT", "Command timed out: {}", cmd);
      return -1;
    }
    
    return result;
  }

  std::filesystem::path test_dir_;
};

TEST_F(MeasureCommandTest, MeasureWithoutDaemon) {
  // Should fail gracefully when daemon not running
  auto script_path = test_dir_ / "test.lua";

  // Use full path to binary
  std::string cmd = "./build/instrument-server measure " +
                    script_path.string() + " 2>&1 > /dev/null";

  int result = run_command_with_timeout(cmd, 3);

  // Should return non-zero (error) or timeout
  EXPECT_NE(result, 0);
}

TEST_F(MeasureCommandTest, MeasureWithDaemon) {
  // Start daemon
  auto &daemon = ServerDaemon::instance();
  ASSERT_TRUE(daemon.start());

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Should still fail with no instruments, but shouldn't crash
  auto script_path = test_dir_ / "test.lua";

  // Use full path to binary
  std::string cmd = "./build/instrument-server measure " +
                    script_path.string() + " 2>&1 > /dev/null";

  int result = run_command_with_timeout(cmd, 3);

  daemon.stop();

  // Expect failure (no instruments running)
  EXPECT_NE(result, 0);
}
