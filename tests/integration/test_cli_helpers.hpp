#pragma once

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <string>
#include <thread>

#ifndef ISS_BIN_PATH
#define ISS_BIN_PATH "instrument-script-server"
#endif

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "data"
#endif

#ifndef TEST_PLUGIN_DIR
#define TEST_PLUGIN_DIR "."
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define popen _popen
#define pclose _pclose
#else
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef _WIN32
inline const std::string ext = ".dll";
#else
inline const std::string ext = ".so";
#endif

inline std::atomic<bool> g_interrupted{false};
inline const std::string bin_path = ISS_BIN_PATH;
inline const std::string data_dir = TEST_DATA_DIR;
inline const std::string mock_plugin =
    (std::filesystem::path(TEST_PLUGIN_DIR) / ("libmock_visa_plugin" + ext))
        .string();
inline const std::string mock_large_plugin =
    (std::filesystem::path(TEST_PLUGIN_DIR) /
     ("libmock_visa_large_data_plugin" + ext))
        .string();
inline std::mutex g_pid_mutex;
inline std::set<int> g_daemon_pids;

using namespace std::chrono_literals;

namespace {

inline bool process_alive(int pid) {
  if (pid <= 0) {
    return false;
  }
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

inline void cleanup_all_daemons() {
  std::lock_guard<std::mutex> lock(g_pid_mutex);

  for (int pid : g_daemon_pids) {
    if (pid <= 0) {
      continue;
    }

#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (h) {
      TerminateProcess(h, 1);
      CloseHandle(h);
    }
#else
    if (!process_alive(pid)) {
      continue;
    }

    kill(pid, SIGTERM);

    for (int i = 0; i < 20; ++i) {
      if (!process_alive(pid)) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (process_alive(pid)) {
      kill(pid, SIGKILL);
    }
#endif
  }

  g_daemon_pids.clear();
}

inline void handle_sigint(int /*unused*/) {
  g_interrupted = true;
  cleanup_all_daemons();
  std::exit(130);
}

struct SignalSetup {
  SignalSetup() {
    std::signal(SIGINT, handle_sigint);
#ifdef SIGTERM
    std::signal(SIGTERM, handle_sigint);
#endif
  }
};

inline SignalSetup g_signal_setup;

struct GlobalCleanup {
  ~GlobalCleanup() { cleanup_all_daemons(); }
};

inline GlobalCleanup g_cleanup;

inline void register_daemon_pid(int pid) {
  if (pid <= 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(g_pid_mutex);
  g_daemon_pids.insert(pid);
}

inline std::string get_runtime_dir() {
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

inline int get_pid_from_file(const std::string &path) {
  std::ifstream ifs(path);
  int pid = 0;
  if (!(ifs >> pid)) {
    return -1;
  }
  return pid;
}

inline std::pair<int, std::string> run_command(const std::string &args) {
#ifdef _WIN32
  // --- Parse command line into exe + args ---
  std::string cmd = args;

  // Extract executable (first token, possibly quoted)
  std::string exe;
  std::string rest;

  if (!cmd.empty() && cmd[0] == '"') {
    size_t end = cmd.find('"', 1);
    if (end != std::string::npos) {
      exe = cmd.substr(1, end - 1);
      rest = cmd.substr(end + 1);
    }
  } else {
    size_t space = cmd.find(' ');
    if (space != std::string::npos) {
      exe = cmd.substr(0, space);
      rest = cmd.substr(space + 1);
    } else {
      exe = cmd;
    }
  }

  // Build full command line (CreateProcess requirement)
  std::string full_cmd = "\"" + exe + "\" " + rest;

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE readPipe = NULL;
  HANDLE writePipe = NULL;

  if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
    return {-1, ""};
  }

  SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si{};
  PROCESS_INFORMATION pi{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = writePipe;
  si.hStdError = writePipe;

  std::vector<char> cmd_buf(full_cmd.begin(), full_cmd.end());
  cmd_buf.push_back('\0');

  BOOL ok = CreateProcessA(NULL, cmd_buf.data(), NULL, NULL, TRUE,
                           CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

  CloseHandle(writePipe);

  if (!ok) {
    CloseHandle(readPipe);
    DWORD err = GetLastError();
    return {-1, "CreateProcess failed: " + std::to_string(err)};
  }

  std::string output;
  char buffer[256];
  DWORD read;

  while (ReadFile(readPipe, buffer, sizeof(buffer), &read, NULL) && read > 0) {
    output.append(buffer, read);
  }

  CloseHandle(readPipe);

  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return {static_cast<int>(exit_code), output};

#else
  // --- POSIX path unchanged ---
  std::string cmd = args + " 2>&1";

  FILE *pipe = popen(cmd.c_str(), "r");
  if (pipe == nullptr) {
    return {-1, ""};
  }

  std::ostringstream output;
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe)) {
    output << buffer;
  }

  int exit_code = pclose(pipe);

  if (WIFEXITED(exit_code)) {
    exit_code = WEXITSTATUS(exit_code);
  }

  return {exit_code, output.str()};
#endif
}

inline int extract_pid(const std::string &input) {
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

inline std::pair<int, std::string> run_iss(const std::string &args) {
  auto result = run_command("\"" + bin_path + "\" " + args);

  if (args.starts_with("daemon start --json")) {
    int pid = extract_pid(result.second);
    if (pid > 0) {
      register_daemon_pid(pid);
    }
  }

  return result;
}

inline bool extract_running(const std::string &input) {
  try {
    nlohmann::json json = nlohmann::json::parse(input);
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

inline bool wait_for_daemon_stopped(int timeout_ms = 5000) {
  for (int waited = 0; waited < timeout_ms; waited += 100) {
    auto [_, out] = run_iss("daemon status --json");
    if (!extract_running(out)) {
      return true;
    }
    std::this_thread::sleep_for(100ms);
  }
  return false;
}

inline bool wait_for_daemon_started(int timeout_ms = 3000) {
  for (int waited = 0; waited < timeout_ms; waited += 100) {
    auto [_, out] = run_iss("daemon status --json");
    if (extract_running(out)) {
      return true;
    }
    std::this_thread::sleep_for(100ms);
  }
  return false;
}

inline std::string extract_first_buffer_id(const std::string &output) {
  std::istringstream ss(output);
  std::string line;
  while (std::getline(ss, line)) {
    auto first_non_space = line.find_first_not_of(" \t");
    if (first_non_space == std::string::npos)
      continue;
    std::string trimmed = line.substr(first_non_space);
    if (trimmed.starts_with("Active buffers:") ||
        trimmed.starts_with("No active"))
      continue;
    if (trimmed.starts_with("- ")) {
      trimmed = trimmed.substr(2);
    }
    auto first_space = trimmed.find(' ');
    std::string id = (first_space != std::string::npos)
                         ? trimmed.substr(0, first_space)
                         : trimmed;
    if (!id.empty()) {
      return id;
    }
  }
  return "";
}

inline void start_instrument(const std::filesystem::path &config) {
  auto [exit_code, output] =
      run_iss("inst start " + config.string() + " --plugin " + mock_plugin);
  EXPECT_EQ(exit_code, 0) << "Instrument start failed, output:\n" << output;
  std::this_thread::sleep_for(200ms);
}

inline void stop_instrument(const std::string &instrument_name) {
  auto [exit_code, output] = run_iss("inst stop " + instrument_name);
  EXPECT_EQ(exit_code, 0) << "Instrument stop failed, output:\n" << output;
  std::this_thread::sleep_for(200ms);
}

inline void start_mock1() {
  start_instrument(std::filesystem::path(data_dir) / "mock_instrument1.yaml");
}

inline void stop_mock1() { stop_instrument("MockInstrument1"); }

inline void start_mock2() {
  start_instrument(std::filesystem::path(data_dir) / "mock_instrument2.yaml");
}

inline void stop_mock2() { stop_instrument("MockInstrument2"); }

} // namespace
