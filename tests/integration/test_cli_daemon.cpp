#include "test_cli_helpers.hpp"

// ---------------------------------------------------------------------------
// CLITestNoAutostart – daemon must NOT be running when these start
// ---------------------------------------------------------------------------

TEST(CLITestNoAutostart, HelpCommand) {
  auto [exit_code, output] = run_iss("--help");
  EXPECT_EQ(exit_code, 0) << "Help command failed: " << exit_code;
  EXPECT_FALSE(output.empty()) << "Help command produced no output";
  bool has_help_content = (output.find("Usage") != std::string::npos ||
                           output.find("Commands") != std::string::npos ||
                           output.find("Options") != std::string::npos ||
                           output.find("help") != std::string::npos);
  EXPECT_TRUE(has_help_content)
      << "Help output doesn't contain expected content";
}

TEST(CLITestNoAutostart, DaemonStatusWhenNotRunning) {
  auto [exit_code, output] = run_iss("daemon status");
  EXPECT_NE(exit_code, 0) << "Expected non-zero when daemon not running";
  EXPECT_NE(exit_code, -1) << "Command failed to execute";
  EXPECT_FALSE(output.empty()) << "Status command produced no output";
}

TEST(CLITestNoAutostart, StartCreatesProcessAndPidFile) {
  auto [start_exit, start_out] = run_iss("daemon start --json");
  ASSERT_EQ(start_exit, 0) << "daemon start failed:\n" << start_out;
  EXPECT_FALSE(start_out.empty()) << "Start command produced no output";
  EXPECT_NE(start_out.find("Daemon started"), std::string::npos)
      << "No 'Daemon started' message:\n"
      << start_out;

  ASSERT_TRUE(wait_for_daemon_started())
      << "Daemon did not become reachable after start";

  auto [exit_code, output] = run_iss("daemon status --json");
  int pid = extract_pid(output);
  bool running = extract_running(output);
  EXPECT_TRUE(running) << "daemon status:\n" << output;
  EXPECT_GT(pid, 0);
  EXPECT_TRUE(process_alive(pid));
  {
    auto [exit_code, output] = run_iss("daemon stop --json");
    EXPECT_EQ(exit_code, 0) << "daemon stop failed:\n" << output;
    for (int i = 0; i < 30; ++i) {
      if (!process_alive(pid)) {
        break;
      }
      std::this_thread::sleep_for(100ms);
    }
  }
  EXPECT_FALSE(process_alive(pid));
}

// ---------------------------------------------------------------------------
// CLITest – daemon started in SetUp, stopped in TearDown
// ---------------------------------------------------------------------------
class CLITestDaemon : public ::testing::Test {
protected:
  void SetUp() override {
    run_iss("daemon start --json");
    ASSERT_TRUE(wait_for_daemon_started())
        << "Daemon never became reachable in SetUp";
  }
  void TearDown() override {
    auto [exit_code, output] = run_iss("daemon stop");
    EXPECT_EQ(exit_code, 0) << "daemon stop failed:\n" << output;
    EXPECT_TRUE(wait_for_daemon_stopped(5000)) << "Daemon never stopped";
  }
};

TEST_F(CLITestDaemon, RestartWorks) {
  auto [status_code1, status_out1] = run_iss("daemon status --json");
  int pid1 = extract_pid(status_out1);
  ASSERT_GT(pid1, 0);

  auto [exit_code, output] = run_iss("daemon stop");
  EXPECT_EQ(exit_code, 0) << "daemon stop failed:\n" << output;
  ASSERT_TRUE(wait_for_daemon_stopped()) << "First daemon never stopped";

  auto [exit_code2, output2] = run_iss("daemon start --json");
  ASSERT_EQ(exit_code2, 0) << "Second daemon start failed:\n" << output2;
  ASSERT_TRUE(wait_for_daemon_started())
      << "Second daemon never became reachable";

  auto [status_code2, status_out2] = run_iss("daemon status --json");
  int pid2 = extract_pid(status_out2);
  ASSERT_GT(pid2, 0);

  EXPECT_NE(pid1, pid2);
}

TEST_F(CLITestDaemon, MultipleStartsDoNotDuplicate) {
  auto [status_code1, status_out1] = run_iss("daemon status --json");
  int pid1 = extract_pid(status_out1);
  ASSERT_GT(pid1, 0);

  run_iss("daemon start --json"); // should be refused
  auto [status_code2, status_out2] = run_iss("daemon status --json");
  int pid2 = extract_pid(status_out2);
  ASSERT_GT(pid2, 0);

  EXPECT_EQ(pid1, pid2); // same daemon
}
