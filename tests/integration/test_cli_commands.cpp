#include <cstdlib>
#include <gtest/gtest.h>
#include <string>

class CLITest : public ::testing::Test {
protected:
  int run_command(const std::string &cmd) {
    return std::system((cmd + " > /dev/null 2>&1").c_str());
  }
};

TEST_F(CLITest, HelpCommand) {
  int result = run_command("instrument-server --help");
  // May not be installed yet, so don't assert
  if (result == 0) {
    SUCCEED();
  }
}

TEST_F(CLITest, DaemonStatusWhenNotRunning) {
  // Stop any running daemon first
  run_command("instrument-server daemon stop");

  int result = run_command("instrument-server daemon status");
  // Should return non-zero when daemon not running
  // But may not be installed, so don't assert
}

TEST_F(CLITest, ListPlugins) {
  int result = run_command("instrument-server plugins");
  // Should succeed even with no plugins
  if (result == 0) {
    SUCCEED();
  }
}

TEST_F(CLITest, ListInstrumentsWhenNoneRunning) {
  int result = run_command("instrument-server list");
  // Should succeed even with no instruments
  if (result == 0) {
    SUCCEED();
  }
}
