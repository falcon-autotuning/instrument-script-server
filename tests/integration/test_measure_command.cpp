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
  int run_command_with_timeout(const std::string &cmd,
                               int timeout_seconds = 5) {
    auto future = std::async(std::launch::async,
                             [&cmd]() { return std::system(cmd.c_str()); });

    if (future.wait_for(std::chrono::seconds(timeout_seconds)) ==
        std::future_status::timeout) {
      LOG_ERROR("TEST", "TIMEOUT", "Command timed out:  {}", cmd);
      // Command timed out, this is expected behavior for measure without daemon
      return -1;
    }

    return future.get();
  }

  std::filesystem::path test_dir_;
};

TEST_F(MeasureCommandTest, MeasureWithoutDaemon) {
  // Should fail gracefully when daemon not running
  auto script_path = test_dir_ / "test.lua";

  // Use timeout wrapper to prevent hanging
  std::string cmd = "timeout 3 instrument-server measure " +
                    script_path.string() + " 2>&1 > /dev/null || echo $?";

  int result = run_command_with_timeout(cmd, 5);

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

  std::string cmd = "timeout 3 instrument-server measure " +
                    script_path.string() + " 2>&1 > /dev/null || echo $?";

  int result = run_command_with_timeout(cmd, 5);

  daemon.stop();

  // Expect failure (no instruments running)
  EXPECT_NE(result, 0);
}
