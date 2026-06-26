
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>

#ifndef ISS_BIN_PATH
#define ISS_BIN_PATH "instrument-script-server"
#endif

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "data"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define popen _popen
#define pclose _pclose
#endif

using namespace std::chrono_literals;

namespace {
const std::string bin_path = ISS_BIN_PATH;
const std::string data_dir = TEST_DATA_DIR;
const std::string mock_plugin = "./libmock_visa_plugin.so";

std::string get_runtime_dir() {
  const char *forced = getenv("INSTRUMENT_SERVER_RUNTIME_DIR");
#ifdef _WIN32
  if (forced != nullptr) {
    return forced;
  }
  char *appdata = getenv("LOCALAPPDATA");
  if (appdata != nullptr) {
    return std::string(appdata) + "\\InstrumentServer";
  }
  return ".\\instrument-script-server-runtime";
#else
  if (forced != nullptr) {
    return forced;
  }
  char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
  if (xdg_runtime != nullptr) {
    return std::string(xdg_runtime) + "/instrument-script-server";
  }
  return "/tmp/instrument-script-server-" +
         std::string((getenv("USER") != nullptr) ? getenv("USER") : "unknown");
#endif
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
  if (pid <= 0)
    return false; // guard: kill(-1,0) or kill(0,0) are dangerous
#ifdef _WIN32
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
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

// Run a command and capture combined stdout+stderr; return {exit_code, output}.
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

std::pair<int, std::string> run_iss(const std::string &args) {
  return run_command("\"" + bin_path + "\" " + args);
}

int extract_pid(const std::string &input) {
  try {
    nlohmann::json json = nlohmann::json::parse(input);

    if (json.contains("pid") && !json["pid"].is_null()) {
      return json["pid"];
    }

    if (json.contains("output") && json["output"].is_array() &&
        !json["output"].empty()) {

      const auto &first = json["output"][0];

      if (first.contains("pid") && !first["pid"].is_null()) {
        return first["pid"];
      }
    }

  } catch (const nlohmann::json::parse_error &e) {
    std::cerr << "JSON parse error: " << e.what() << "\n";
  } catch (const std::exception &e) {
    std::cerr << "Error processing JSON: " << e.what() << "\n";
  }
  std::cout << "The JSON that should have contained 'pid' looks like:\n"
            << input << "\n";
  return -1;
}

bool extract_running(const std::string &input) {
  try {
    nlohmann::json json = nlohmann::json::parse(input);
    if (json.contains("running") && !json["running"].is_null()) {
      if (json["running"].is_boolean())
        return json["running"].get<bool>();
      if (json["running"].is_number())
        return json["running"].get<int>() != 0;
    }
    if (json.contains("output") && json["output"].is_array() &&
        !json["output"].empty()) {
      const auto &first = json["output"][0];
      if (first.contains("running") && !first["running"].is_null()) {
        if (first["running"].is_boolean())
          return first["running"].get<bool>();
        if (first["running"].is_number())
          return first["running"].get<int>() != 0;
      }
    }
  } catch (...) {
  }
  return false;
}

// Poll until daemon status says not-running (or 3 s timeout).
bool wait_for_daemon_stopped(int timeout_ms = 3000) {
  for (int waited = 0; waited < timeout_ms; waited += 100) {
    auto [_, out] = run_iss("daemon status --json");
    if (!extract_running(out)) {
      return true;
    }
    std::this_thread::sleep_for(100ms);
  }
  return false;
}

// Poll until daemon status says running (or 3 s timeout).
bool wait_for_daemon_started(int timeout_ms = 3000) {
  for (int waited = 0; waited < timeout_ms; waited += 100) {
    auto [_, out] = run_iss("daemon status --json");
    if (extract_running(out)) {
      return true;
    }
    std::this_thread::sleep_for(100ms);
  }
  return false;
}

// Extract the first buffer ID from `list-buffers` output lines:
//   "  - <id> (<N> elements, type=<T>)"
std::string extract_first_buffer_id(const std::string &output) {
  std::istringstream ss(output);
  std::string line;
  while (std::getline(ss, line)) {
    auto dash = line.find("- ");
    if (dash == std::string::npos)
      continue;
    auto id_start = dash + 2;
    auto id_end = line.find(' ', id_start);
    if (id_end == std::string::npos)
      id_end = line.size();
    auto id = line.substr(id_start, id_end - id_start);
    if (!id.empty())
      return id;
  }
  return "";
}

} // namespace

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

TEST(CLITestNoAutostart, DaemonStartStop) {
  auto [exit_code, output] = run_iss("daemon start --json");
  EXPECT_EQ(exit_code, 0) << "daemon start failed:\n" << output;
  EXPECT_FALSE(output.empty()) << "Start command produced no output";
  EXPECT_NE(output.find("Daemon started"), std::string::npos)
      << "No 'Daemon started' message:\n"
      << output;
  {
    auto [exit_code, output] = run_iss("daemon stop");
    EXPECT_EQ(exit_code, 0) << "daemon stop failed:\n" << output;
    std::this_thread::sleep_for(100ms);
    EXPECT_TRUE(wait_for_daemon_stopped()) << "Daemon never stopped";
  }
}

TEST(CLITestNoAutostart, StartCreatesProcessAndPidFile) {
  auto [start_exit, start_out] = run_iss("daemon start --json");
  // If the daemon refused to start we can't test anything below.
  ASSERT_EQ(start_exit, 0) << "daemon start failed:\n" << start_out;

  ASSERT_TRUE(wait_for_daemon_started())
      << "Daemon did not become reachable after start";

  auto [exit_code, output] = run_iss("daemon status --json");
  int pid = extract_pid(output);
  bool running = extract_running(output);
  EXPECT_TRUE(running) << "daemon status:\n" << output;
  EXPECT_GT(pid, 0);
  EXPECT_TRUE(process_alive(pid));

  EXPECT_EQ(exit_code, 0) << "daemon stop failed:\n" << output;
  for (int i = 0; i < 30; ++i) {
    if (!process_alive(pid)) {
      break;
    }
    std::this_thread::sleep_for(100ms);
  }
  EXPECT_FALSE(process_alive(pid));
}

// ---------------------------------------------------------------------------
// CLITest – daemon started in SetUp, stopped in TearDown
// ---------------------------------------------------------------------------
class CLITest : public ::testing::Test {
protected:
  void SetUp() override {
    std::system((bin_path + " daemon start").c_str());
    // Wait for gRPC to be ready, not just for the process to exist
    ASSERT_TRUE(wait_for_daemon_started())
        << "Daemon never became reachable in SetUp";
  }
  void TearDown() override {
    auto [exit_code, output] = run_iss("daemon stop");
    EXPECT_EQ(exit_code, 0) << "daemon stop failed:\n" << output;
    EXPECT_TRUE(wait_for_daemon_stopped()) << "Daemon never stopped";
  }
};

// --- Daemon management ---

TEST_F(CLITest, RestartWorks) {
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

TEST_F(CLITest, MultipleStartsDoNotDuplicate) {
  auto [status_code1, status_out1] = run_iss("daemon status --json");
  int pid1 = extract_pid(status_out1);
  ASSERT_GT(pid1, 0);

  run_iss("daemon start --json"); // should be refused
  auto [status_code2, status_out2] = run_iss("daemon status --json");
  int pid2 = extract_pid(status_out2);
  ASSERT_GT(pid2, 0);

  EXPECT_EQ(pid1, pid2); // same daemon
}

// --- Discover / plugin listing ---

TEST_F(CLITest, ListPlugins) {
  auto [exit_code, output] = run_iss("discover");
  EXPECT_EQ(exit_code, 0) << "discover failed: " << exit_code;
  EXPECT_NE(output, "") << "There was no output";
}

// --- Instrument lifecycle ---

TEST_F(CLITest, ListInstrumentsWhenNoneRunning) {
  auto [exit_code, output] = run_iss("list");
  EXPECT_EQ(exit_code, 1) << "list should return 1 when no instruments: "
                          << exit_code;
  EXPECT_NE(output, "") << "There was no output";
}

TEST_F(CLITest, StartInstrument) {
  {
    auto [exit_code, output] = run_iss(
        "start " + data_dir + "/mock_instrument1.yaml --plugin " + mock_plugin);
    EXPECT_EQ(exit_code, 0) << "Instrument start failed, output:\n" << output;
  }
  std::this_thread::sleep_for(100ms);
  {
    auto [exit_code, output] = run_iss("status MockInstrument1");
    EXPECT_EQ(exit_code, 0) << "instrument status failed";
    EXPECT_NE(output.find("RUNNING"), std::string::npos)
        << "MockInstrument1 not RUNNING:\n"
        << output;
  }
  std::this_thread::sleep_for(300ms);
  {
    auto [exit_code, output] = run_iss("stop MockInstrument1");
    EXPECT_EQ(exit_code, 0) << "instrument stop failed";
    EXPECT_NE(output.find("Stopped instrument"), std::string::npos)
        << "MockInstrument1 did not stop:\n"
        << output;
  }
}

TEST_F(CLITest, StartInstruments) {
  run_iss("start " + data_dir + "/mock_instrument1.yaml --plugin " +
          mock_plugin);
  std::this_thread::sleep_for(100ms);
  {
    auto [exit_code, output] = run_iss("status MockInstrument1");
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("RUNNING"), std::string::npos)
        << "MockInstrument1 not running:\n"
        << output;
  }

  run_iss("start " + data_dir + "/mock_instrument2.yaml --plugin " +
          mock_plugin);
  std::this_thread::sleep_for(100ms);
  {
    auto [exit_code, output] = run_iss("status MockInstrument2");
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("RUNNING"), std::string::npos)
        << "MockInstrument2 not running:\n"
        << output;
  }

  {
    auto [exit_code, output] = run_iss("stop MockInstrument1");
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("Stopped instrument"), std::string::npos) << output;
  }
  {
    auto [exit_code, output] = run_iss("stop MockInstrument2");
    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(output.find("Stopped instrument"), std::string::npos) << output;
  }
}

// ---------------------------------------------------------------------------
// Measure tests (all under CLITest so daemon is already running)
// ---------------------------------------------------------------------------

// Helper: start MockInstrument1 and wait for RUNNING state.
static void start_mock1() {
  run_iss("start " + std::string(TEST_DATA_DIR) +
          "/mock_instrument1.yaml --plugin ./libmock_visa_plugin.so");
  std::this_thread::sleep_for(200ms);
}

static void stop_mock1() { run_iss("stop MockInstrument1"); }

static void start_mock2() {
  run_iss("start " + std::string(TEST_DATA_DIR) +
          "/mock_instrument2.yaml --plugin ./libmock_visa_plugin.so");
  std::this_thread::sleep_for(200ms);
}

static void stop_mock2() { run_iss("stop MockInstrument2"); }

// --- measure – basic ---

TEST_F(CLITest, SimpleMeasure) {
  start_mock1();

  auto [rc, out] =
      run_iss("measure " + data_dir + "/test_scripts/simple_call.lua");
  EXPECT_EQ(rc, 0) << "measure returned non-zero:\n" << out;
  EXPECT_NE(out.find("Measurement complete"), std::string::npos)
      << "Expected 'Measurement complete':\n"
      << out;

  stop_mock1();
}

TEST_F(CLITest, MeasureJsonOutputHasResultFields) {
  start_mock1();

  auto [rc, out] = run_iss("measure " + data_dir +
                           "/test_scripts/multiple_returns.lua --json");
  ASSERT_EQ(rc, 0) << "measure --json failed:\n" << out;

  nlohmann::json j;
  ASSERT_NO_THROW(j = nlohmann::json::parse(out))
      << "Output is not valid JSON:\n"
      << out;

  EXPECT_TRUE(j.value("ok", false)) << j.dump(2);
  ASSERT_TRUE(j.contains("results") && j["results"].is_array())
      << "JSON missing 'results' array:\n"
      << j.dump(2);

  for (const auto &r : j["results"]) {
    EXPECT_TRUE(r.contains("instrument"))
        << "Result entry missing 'instrument': " << r;
    EXPECT_TRUE(r.contains("verb")) << "Result entry missing 'verb': " << r;
  }

  stop_mock1();
}

TEST_F(CLITest, LoopMeasurementCompletes) {
  start_mock1();

  auto [rc, out] =
      run_iss("measure " + data_dir + "/test_scripts/loop_measurement.lua");
  EXPECT_EQ(rc, 0) << "Loop measurement failed:\n" << out;
  EXPECT_NE(out.find("Measurement complete"), std::string::npos)
      << "Expected 'Measurement complete':\n"
      << out;

  stop_mock1();
}

TEST_F(CLITest, MeasureNonExistentScriptFails) {
  auto [rc, out] = run_iss("measure /tmp/does_not_exist_xyz_1234.lua");
  EXPECT_NE(rc, 0) << "Expected failure for missing script:\n" << out;
}

// --- measure – two instruments ---

TEST_F(CLITest, TwoInstrumentMeasureCompletes) {
  start_mock1();
  start_mock2();

  auto [rc, out] =
      run_iss("measure " + data_dir + "/test_scripts/two_instruments.lua");
  EXPECT_EQ(rc, 0) << "Two-instrument measurement failed:\n" << out;
  EXPECT_NE(out.find("Measurement complete"), std::string::npos)
      << "Expected 'Measurement complete':\n"
      << out;

  stop_mock1();
  stop_mock2();
}

TEST_F(CLITest, TwoInstrumentJsonContainsBothInstruments) {
  start_mock1();
  start_mock2();

  auto [rc, out] = run_iss("measure " + data_dir +
                           "/test_scripts/two_instruments.lua --json");
  ASSERT_EQ(rc, 0) << out;

  nlohmann::json j;
  ASSERT_NO_THROW(j = nlohmann::json::parse(out)) << "Not valid JSON:\n" << out;

  // The job must report overall success.
  EXPECT_TRUE(j.value("ok", false)) << j.dump(2);

  // The results array may be empty if the script does not explicitly return
  // MeasurementResponse objects, but its presence is still required.
  EXPECT_TRUE(j.contains("results") && j["results"].is_array())
      << "JSON missing 'results' array:\n"
      << j.dump(2);

  stop_mock1();
  stop_mock2();
}

// --- buffer commands ---

// list-buffers with no buffers should succeed and say so.
TEST_F(CLITest, ListBuffersWhenEmpty) {
  auto [rc, out] = run_iss("list-buffers");
  EXPECT_EQ(rc, 0) << "list-buffers failed:\n" << out;
  EXPECT_NE(out.find("No active shared memory buffers"), std::string::npos)
      << "Expected 'No active shared memory buffers':\n"
      << out;
}

// read-buffer with a non-existent ID should fail cleanly.
TEST_F(CLITest, ReadNonExistentBufferFails) {
  auto [rc, out] = run_iss("read-buffer this_id_does_not_exist_xyz");
  EXPECT_NE(rc, 0) << "Expected non-zero exit for bad buffer ID:\n" << out;
}

// buffer-metadata with a non-existent ID should fail cleanly.
TEST_F(CLITest, MetadataForNonExistentBufferFails) {
  auto [rc, out] = run_iss("buffer-metadata this_id_does_not_exist_xyz");
  EXPECT_NE(rc, 0) << "Expected non-zero exit for bad buffer ID:\n" << out;
}

// release-buffer with a non-existent ID is idempotent: the server
// returns ok:true as a no-op rather than treating an unknown ID as an error.
TEST_F(CLITest, ReleaseNonExistentBufferIsIdempotent) {
  auto [rc, out] = run_iss("release-buffer this_id_does_not_exist_xyz");
  EXPECT_EQ(rc, 0) << "Expected idempotent (exit 0) for unknown buffer ID:\n"
                   << out;
}

// read-buffer requires an argument.
TEST_F(CLITest, ReadBufferMissingArgFails) {
  auto [rc, out] = run_iss("read-buffer");
  EXPECT_NE(rc, 0) << "Expected non-zero when buffer ID omitted:\n" << out;
}
