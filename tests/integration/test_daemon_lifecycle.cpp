#include "instrument-server/Logger.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/ServerDaemon.hpp"
#include <gtest/gtest.h>
#include <thread>

using namespace instserver;

class DaemonLifecycleTest : public ::testing::Test {
protected:
  void SetUp() override {
    InstrumentLogger::instance().init("test_daemon. log", spdlog::level::debug);

    // Clean up any existing daemon
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
  }

  void TearDown() override {
    auto &daemon = ServerDaemon::instance();
    if (daemon.is_running()) {
      daemon.stop();
    }
  }
};

TEST_F(DaemonLifecycleTest, StartDaemon) {
  auto &daemon = ServerDaemon::instance();

  EXPECT_TRUE(daemon.start());
  EXPECT_TRUE(daemon.is_running());
  EXPECT_TRUE(ServerDaemon::is_already_running());

  daemon.stop();
}

TEST_F(DaemonLifecycleTest, DaemonPersistsAcrossCommands) {
  auto &daemon = ServerDaemon::instance();

  ASSERT_TRUE(daemon.start());

  // Get registry (simulates 'start' command)
  auto &registry = InstrumentRegistry::instance();

  // Daemon should still be running
  EXPECT_TRUE(daemon.is_running());

  // Simulates another command accessing registry
  auto instruments = registry.list_instruments();

  // Daemon should still be running
  EXPECT_TRUE(daemon.is_running());

  daemon.stop();
}

TEST_F(DaemonLifecycleTest, RegistryAccessWithoutDaemon) {
  // Should be able to access registry even without daemon for testing
  auto &registry = InstrumentRegistry::instance();
  auto instruments = registry.list_instruments();

  EXPECT_TRUE(instruments.empty());
}
