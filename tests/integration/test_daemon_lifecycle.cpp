#include "instrument-script-server/plugin/PluginRegistry.hpp"
#include "instrument-script-server/server/InstrumentRegistry.hpp"
#include "instrument-script-server/server/ServerDaemon.hpp"
#include <gtest/gtest.h>
#include <instrument-log/inst_logging.h>
#include <thread>

using namespace instserver;

class DaemonLifecycleTest : public ::testing::Test {
protected:
  void SetUp() override {

    inst_log_shutdown();
    inst_log_init("test_daemon.log", INST_LOG_DEBUG, "daemon_test",
                  1024 * 1024, // 1 MB
                  3);          // rotation count
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
    // Clean up after each test - use public API only
    auto &daemon = ServerDaemon::instance();
    auto &plugin_registry = instserver::plugin::PluginRegistry::instance();
    plugin_registry.unload_all();
    if (daemon.is_running()) {
      daemon.stop();
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    inst_log_flush();
    inst_log_shutdown();
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
