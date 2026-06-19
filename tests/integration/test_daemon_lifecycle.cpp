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
#ifdef _WIN32
static std::string BIN_PATH = "instrument-script-server.exe";
#else
static std::string BIN_PATH = "./../instrument-script-server";
#endif

static std::string run_cmd(const std::string &cmd) {
#ifdef _WIN32
  FILE *pipe = _popen(cmd.c_str(), "r");
#else
  FILE *pipe = popen(cmd.c_str(), "r");
#endif
  if (pipe == nullptr) {
    return "";
  }

  char buffer[256];
  std::string result;
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    result += buffer;
  }

#ifdef _WIN32
  _pclose(pipe);
#else
  pclose(pipe);
#endif

  return result;
}

static int get_pid_from_file(const std::string &path) {
  std::ifstream ifs(path);
  int pid;
  if (!(ifs >> pid))
    return -1;
  return pid;
}

static bool process_alive(int pid) {
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
namespace {
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
    std::system((BIN_PATH + " daemon stop").c_str());
    std::this_thread::sleep_for(200ms);
  }

  void TearDown() override {
    std::system((BIN_PATH + " daemon stop").c_str());
    std::this_thread::sleep_for(200ms);
  }
};

TEST_F(DaemonIntegrationTest, StartCreatesProcessAndPidFile) {
  int rc = std::system((BIN_PATH + " daemon start --json").c_str());
  // std::cout << "The start result is " << out << "\n";
  std::string out;
  int pid = -1;
  std::this_thread::sleep_for(200ms);
  out = run_cmd(BIN_PATH + " daemon status --json");
  std::cout << "The daemon status is " << out << "\n";
  pid = extract_pid(out);
  bool running = extract_running(out);
  EXPECT_TRUE(running);

  EXPECT_GT(pid, 0);
  EXPECT_TRUE(process_alive(pid));
  std::system((BIN_PATH + " daemon stop").c_str());
  std::this_thread::sleep_for(200ms);
  EXPECT_FALSE(process_alive(pid));
}

TEST_F(DaemonIntegrationTest, RestartWorks) {
  int rc1 = std::system((BIN_PATH + " daemon start --json").c_str());
  ASSERT_EQ(rc1, 0);
  std::this_thread::sleep_for(200ms);

  std::string out1 = run_cmd(BIN_PATH + " daemon status --json");
  int pid1 = extract_pid(out1);
  ASSERT_GT(pid1, 0);

  std::system((BIN_PATH + " daemon stop").c_str());
  std::this_thread::sleep_for(300ms);

  int rc2 = std::system((BIN_PATH + " daemon start --json").c_str());
  ASSERT_EQ(rc2, 0);
  std::this_thread::sleep_for(200ms);

  std::string out2 = run_cmd(BIN_PATH + " daemon status --json");
  int pid2 = extract_pid(out2);
  ASSERT_GT(pid2, 0);

  EXPECT_NE(pid1, pid2);

  std::system((BIN_PATH + " daemon stop").c_str());
}

TEST_F(DaemonIntegrationTest, MultipleStartsDoNotDuplicate) {
  int rc1 = std::system((BIN_PATH + " daemon start --json").c_str());
  ASSERT_EQ(rc1, 0);
  std::this_thread::sleep_for(200ms);
  std::string out1 = run_cmd(BIN_PATH + " daemon status --json");
  int pid1 = extract_pid(out1);
  ASSERT_GT(pid1, 0);

  int rc2 = std::system((BIN_PATH + " daemon start --json").c_str());
  ASSERT_EQ(rc2, 0);
  std::string out2 = run_cmd(BIN_PATH + " daemon status --json");
  int pid2 = extract_pid(out2);
  ASSERT_GT(pid2, 0);
  std::this_thread::sleep_for(200ms);

  EXPECT_EQ(pid1, pid2); // same daemon
  std::system((BIN_PATH + " daemon stop").c_str());
}
