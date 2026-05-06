#include "instrument-script-server/server/ServerDaemon.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

using namespace instserver;

class ServerDaemonTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Clean up any stale daemon before each test
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
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
  }

  void TearDown() override {
    // Clean up after each test
    if (ServerDaemon::is_already_running()) {
      auto &daemon = ServerDaemon::instance();
      daemon.stop();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
};

TEST_F(ServerDaemonTest, GetPaths) {
  std::string pid_file = ServerDaemon::get_pid_file_path();
  std::string shutdown_pipe = ServerDaemon::get_shutdown_pipe_path();

  EXPECT_FALSE(pid_file.empty());
  EXPECT_FALSE(shutdown_pipe.empty());
  EXPECT_NE(pid_file, shutdown_pipe);

#ifdef _WIN32
  // On Windows, pipe should start with \\.\pipe\
  EXPECT_TRUE(shutdown_pipe.find("\\\\.\\pipe\\") != std::string::npos);
#else
  // On Unix, pipe should end with .pipe
  EXPECT_TRUE(shutdown_pipe.find(".pipe") != std::string::npos);
#endif
}

TEST_F(ServerDaemonTest, IsRunningWhenNotStarted) {
  EXPECT_FALSE(ServerDaemon::is_already_running());
}

TEST_F(ServerDaemonTest, StartStop) {
  auto &daemon = ServerDaemon::instance();

  ASSERT_TRUE(daemon.start());
  EXPECT_TRUE(daemon.is_running());
  EXPECT_TRUE(ServerDaemon::is_already_running());

  int pid = ServerDaemon::get_daemon_pid();
  EXPECT_GT(pid, 0);

  daemon.stop();
  EXPECT_FALSE(daemon.is_running());

  // Give cleanup time
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_FALSE(ServerDaemon::is_already_running());
}

TEST_F(ServerDaemonTest, PreventMultipleInstances) {
  auto &daemon = ServerDaemon::instance();

  ASSERT_TRUE(daemon.start());
  EXPECT_TRUE(daemon.is_running());

  // Try to start again - should fail and log warning
  EXPECT_TRUE(daemon.start()); // Returns true but doesn't actually start again
  EXPECT_TRUE(daemon.is_running()); // Should still be running from first start

  daemon.stop();
}

TEST_F(ServerDaemonTest, PidFileCreationAndCleanup) {
  auto &daemon = ServerDaemon::instance();
  std::string pid_file = ServerDaemon::get_pid_file_path();

  // PID file should not exist before start
  // (already cleaned up by SetUp)

  ASSERT_TRUE(daemon.start());

  // PID file should exist after start
  EXPECT_TRUE(std::filesystem::exists(pid_file));

  // PID file should contain valid PID
  std::ifstream ifs(pid_file);
  int pid;
  EXPECT_TRUE(ifs >> pid);
  EXPECT_GT(pid, 0);
  ifs.close();

  daemon.stop();

  // PID file should be cleaned up after stop
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_FALSE(std::filesystem::exists(pid_file));
}

TEST_F(ServerDaemonTest, ShutdownPipeCreation) {
  auto &daemon = ServerDaemon::instance();
  std::string shutdown_pipe = ServerDaemon::get_shutdown_pipe_path();

  ASSERT_TRUE(daemon.start());

  // Verify pipe path is valid (actual file/pipe existence is platform-specific)
  EXPECT_FALSE(shutdown_pipe.empty());

#ifdef _WIN32
  // On Windows, verify named pipe path format
  EXPECT_TRUE(shutdown_pipe.find("\\\\.\\pipe\\") != std::string::npos);
  EXPECT_TRUE(shutdown_pipe.find("instrument-server-shutdown") !=
              std::string::npos);
#else
  // On Unix, verify FIFO path format
  EXPECT_TRUE(shutdown_pipe.find("/") != std::string::npos);
  EXPECT_TRUE(shutdown_pipe.find(".pipe") != std::string::npos);
#endif

  daemon.stop();
}

TEST_F(ServerDaemonTest, GracefulShutdownFromCliProcess) {
  auto &daemon = ServerDaemon::instance();

  // Start daemon in main process
  ASSERT_TRUE(daemon.start());
  EXPECT_TRUE(daemon.is_running());

  int daemon_pid = ServerDaemon::get_daemon_pid();
  EXPECT_GT(daemon_pid, 0);

  // Simulate CLI process calling stop (in a separate thread to be realistic)
  std::thread cli_thread([&daemon]() {
    // This simulates what happens when "daemon stop" command is run
    // from a different process
    daemon.stop();
  });

  cli_thread.join();

  // Give daemon time to process shutdown
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  // Verify daemon is actually stopped
  EXPECT_FALSE(daemon.is_running());
  EXPECT_FALSE(ServerDaemon::is_already_running());
}

TEST_F(ServerDaemonTest, StatusAfterStop) {
  auto &daemon = ServerDaemon::instance();

  // Initially not running
  EXPECT_FALSE(ServerDaemon::is_already_running());

  // Start daemon
  ASSERT_TRUE(daemon.start());
  EXPECT_TRUE(daemon.is_running());
  EXPECT_TRUE(ServerDaemon::is_already_running());

  // Stop daemon
  daemon.stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Verify completely stopped
  EXPECT_FALSE(daemon.is_running());
  EXPECT_FALSE(ServerDaemon::is_already_running());

  // Verify PID file is gone
  EXPECT_FALSE(std::filesystem::exists(ServerDaemon::get_pid_file_path()));
}

TEST_F(ServerDaemonTest, RpcPortConfiguration) {
  auto &daemon = ServerDaemon::instance();

  // Set custom RPC port before starting
  uint16_t test_port = 9999;
  daemon.set_rpc_port(test_port);

  EXPECT_EQ(daemon.rpc_port(), test_port);

  // Start and verify
  ASSERT_TRUE(daemon.start());
  EXPECT_TRUE(daemon.is_running());

  daemon.stop();
}

TEST_F(ServerDaemonTest, MultipleStopCalls) {
  auto &daemon = ServerDaemon::instance();

  ASSERT_TRUE(daemon.start());
  EXPECT_TRUE(daemon.is_running());

  // First stop should work
  daemon.stop();
  EXPECT_FALSE(daemon.is_running());

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Second stop should be idempotent (no crash)
  daemon.stop();
  EXPECT_FALSE(daemon.is_running());
}
