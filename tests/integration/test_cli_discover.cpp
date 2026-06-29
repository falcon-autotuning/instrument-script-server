#include "test_cli_helpers.hpp"

class CLITestDiscover : public ::testing::Test {
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

TEST_F(CLITestDiscover, ListPlugins) {
  auto [exit_code, output] = run_iss("discover");
  EXPECT_EQ(exit_code, 0) << "discover failed: " << exit_code;
  EXPECT_FALSE(output.empty()) << "There was no output";
}
