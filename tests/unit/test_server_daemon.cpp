#include "instrument-server/server/ServerDaemon.hpp"

#include <gtest/gtest.h>
#include <thread>

using namespace instserver;

TEST(ServerDaemon, GetPaths) {
  std::string pid_file = ServerDaemon::get_pid_file_path();
  std::string lock_file = ServerDaemon::get_lock_file_path();

  EXPECT_FALSE(pid_file.empty());
  EXPECT_FALSE(lock_file.empty());
  EXPECT_NE(pid_file, lock_file);
}

TEST(ServerDaemon, IsRunningWhenNotStarted) {
  // Clean up any stale daemon
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

  EXPECT_FALSE(ServerDaemon::is_already_running());
}

TEST(ServerDaemon, StartStop) {
  auto &daemon = ServerDaemon::instance();

  ASSERT_TRUE(daemon.start());
  EXPECT_TRUE(daemon.is_running());
  EXPECT_TRUE(ServerDaemon::is_already_running());

  int pid = ServerDaemon::get_daemon_pid();
  EXPECT_GT(pid, 0);

  daemon.stop();
  EXPECT_FALSE(daemon.is_running());

  // Give cleanup time
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_FALSE(ServerDaemon::is_already_running());
}

TEST(ServerDaemon, PreventMultipleInstances) {
  auto &daemon = ServerDaemon::instance();

  ASSERT_TRUE(daemon.start());

  // Try to start again - should ignore and skip
  EXPECT_TRUE(daemon.start());

  daemon.stop();
}
