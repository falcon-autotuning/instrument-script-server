#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using std::system;

using namespace std::chrono_literals;
#ifndef ISS_DAEMON_PATH
#define ISS_DAEMON_PATH "instrument-script-server-daemon"
#endif
namespace {
std::string bin_path = ISS_DAEMON_PATH;

std::pair<int, std::string> run_command(const std::string &args) {
  std::string cmd = args + " 2>&1";

  FILE *pipe = popen(cmd.c_str(), "r");
  if (pipe == nullptr) {
    return {-1, ""};
  }

  std::ostringstream output;
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output << buffer;
  }

  int exit_code = pclose(pipe);

#ifndef _WIN32
  if (WIFEXITED(exit_code)) {
    exit_code = WEXITSTATUS(exit_code);
  }
#endif

  return {exit_code, output.str()};
}

int get_pid_from_file(const std::string &path) {
  std::ifstream ifs(path);
  int pid = 0;
  if (!(ifs >> pid)) {
    return -1;
  }
  return pid;
}

bool process_alive(int pid) {
#ifdef _WIN32
  HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
  if (!h)
    return false;

  DWORD code;
  GetExitCodeProcess(h, &code);
  CloseHandle(h);
  return code == STILL_ACTIVE;
#else
  return (kill(pid, 0) == 0);
#endif
}

int extract_pid(std::string input) {
  nlohmann::json json = nlohmann::json::parse(input);
  if (json.contains("pid") && !json["pid"].is_null()) {
    return json["pid"];
  }
  return -1;
}

bool extract_running(std::string input) {
  nlohmann::json json = nlohmann::json::parse(input);
  if (json.contains("running") && !json["running"].is_null()) {
    return json["running"];
  }
  return false;
}
} // namespace

class DaemonIntegrationTest : public ::testing::Test {
protected:
  void SetUp() override {
    // ensure stopped beforehand
    std::system((bin_path + " daemon stop").c_str());
    std::this_thread::sleep_for(200ms);
  }

  void TearDown() override {
    std::system((bin_path + " daemon stop").c_str());
    std::this_thread::sleep_for(200ms);
  }
};

TEST_F(DaemonIntegrationTest, StartDaemonHelp) {
  auto [exit_code, output] = run_command(bin_path + " --help");
  bool has_help_content =
      output.find("instrument-script-server-daemon") != std::string::npos;
  EXPECT_TRUE(has_help_content)
      << "Help output doesn't contain expected content: " << output;
}

TEST_F(DaemonIntegrationTest, StartDaemonOtherHelp) {
  auto [exit_code, output] = run_command(bin_path + " -h");
  bool has_help_content =
      output.find("instrument-script-server-daemon") != std::string::npos;
  EXPECT_TRUE(has_help_content)
      << "Help output doesn't contain expected content: " << output;
}

// TODO: Add more tests for the arguments like version and log-level
