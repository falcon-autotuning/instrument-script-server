#include "test_cli_helpers.hpp"

class CLITestInstruments : public ::testing::Test {
protected:
  void SetUp() override {
    run_iss("daemon start --json");
    ASSERT_TRUE(wait_for_daemon_started())
        << "Daemon never became reachable in SetUp";
  }
  void TearDown() override {
    run_iss("inst stop MockInstrument1");
    run_iss("inst stop MockInstrument2");
    auto [exit_code, output] = run_iss("daemon stop");
    EXPECT_EQ(exit_code, 0) << "daemon stop failed:\n" << output;
    EXPECT_TRUE(wait_for_daemon_stopped(5000)) << "Daemon never stopped";
  }
};

TEST_F(CLITestInstruments, ListInstrumentsWhenNoneRunning) {
  auto [exit_code, output] = run_iss("inst list");
  EXPECT_EQ(exit_code, 1) << "list should return 1 when no instruments: "
                          << exit_code;
  EXPECT_FALSE(output.empty()) << "There was no output";
}

TEST_F(CLITestInstruments, StartInstrument) {
  start_mock1();
  {
    auto [exit_code, output] = run_iss("inst status MockInstrument1");
    EXPECT_EQ(exit_code, 0) << "instrument status failed";
  }
  std::this_thread::sleep_for(200ms);
  stop_mock1();
}

TEST_F(CLITestInstruments, StartInstruments) {
  start_mock1();
  {
    auto [exit_code, output] = run_iss("inst status MockInstrument1");
    EXPECT_EQ(exit_code, 0);
  }

  start_mock2();
  std::this_thread::sleep_for(100ms);
  {
    auto [exit_code, output] = run_iss("inst status MockInstrument2");
    EXPECT_EQ(exit_code, 0);
  }
  stop_mock1();
  stop_mock2();
}
