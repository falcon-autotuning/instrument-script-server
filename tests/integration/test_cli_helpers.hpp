#pragma once

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
#include <vector>

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
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef _WIN32
inline const std::string ext = ".dll";
#else
inline const std::string ext = ".so";
#endif

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
    HANDLE h =
        OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_INFORMATION,
                    FALSE, pid);

    if (!h) {
      continue;
    }

    // Step 1: Try to terminate (like SIGTERM equivalent)
    TerminateProcess(h, 1);

    // Step 2: Wait for exit (retry loop like Linux)
    bool exited = false;

    for (int i = 0; i < 20; ++i) {
      DWORD wait_result = WaitForSingleObject(h, 100);

      if (wait_result == WAIT_OBJECT_0) {
        exited = true;
        break;
      }
    }

    // Step 3: Escalate if still alive (like SIGKILL)
    if (!exited) {
      // Try again aggressively
      TerminateProcess(h, 1);

      DWORD wait_result = WaitForSingleObject(h, 1000);

      if (wait_result != WAIT_OBJECT_0) {
        // Optional: log hard failure
      }
    }

    // Step 4: Final verification (like process_alive check)
    DWORD exit_code = 0;
    if (GetExitCodeProcess(h, &exit_code) && exit_code == STILL_ACTIVE) {
      // Last resort — something is wrong
    }

    CloseHandle(h);
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

std::pair<int, std::string> run_command(const std::string &args) {
#ifdef _WIN32
  std::vector<char> cmd_buf(args.begin(), args.end());
  cmd_buf.push_back('\0');

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE readPipe = NULL;
  HANDLE writePipe = NULL;
  CreatePipe(&readPipe, &writePipe, &sa, 0);

  SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

  HANDLE hNull =
      CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

  STARTUPINFOA si{};
  PROCESS_INFORMATION pi{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;

  si.hStdOutput = writePipe;
  si.hStdError = writePipe;
  si.hStdInput = hNull;

  std::cout << "CMD: " << args << std::endl;

  BOOL ok = CreateProcessA(NULL, cmd_buf.data(), NULL, NULL, TRUE,
                           CREATE_NO_WINDOW, NULL, NULL, &si, &pi);

  CloseHandle(writePipe);
  CloseHandle(hNull);

  if (!ok) {
    CloseHandle(readPipe);
    return {-1, "CreateProcess failed"};
  }

  std::string output;
  std::thread reader([&] {
    char buffer[256];
    DWORD read = 0;

    while (true) {
      BOOL success = ReadFile(readPipe, buffer, sizeof(buffer), &read, NULL);

      if (success && read > 0) {
        output.append(buffer, read);
      } else {
        DWORD err = GetLastError();
        if (err == ERROR_BROKEN_PIPE || read == 0) {
          break; // ✅ pipe fully drained
        }
      }
    }
  });

  DWORD result = WaitForSingleObject(pi.hProcess, INFINITE);

  if (result != WAIT_OBJECT_0) {
    TerminateProcess(pi.hProcess, 1);
  }

  reader.join();

  CloseHandle(readPipe);

  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return {static_cast<int>(exit_code), output};

#else
  std::string cmd = args + " 2>&1";

  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
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

inline void cleanup_runtime_dir() {
  std::string dir = get_runtime_dir();
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
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
  return 0;
}

inline std::pair<int, std::string> run_iss(const std::string &args) {
  std::string exe = std::filesystem::absolute(bin_path).string();
  auto result = run_command("\"" + exe + "\" " + args);

  if (args.starts_with("daemon start --json")) {
    int pid = extract_pid(result.second);
    if (pid > 0) {
      register_daemon_pid(pid);
    } else {
      std::cerr << "Invalid PID from daemon start: " << pid << "\n";
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

template <typename Fn>
auto call_with_timeout(Fn &&fn, int timeout_ms)
    -> std::optional<decltype(fn())> {
  using Result = decltype(fn());

  std::optional<Result> result;
  std::atomic<bool> done = false;

  std::thread t([&] {
    try {
      result = fn();
    } catch (...) {
      // ignore
    }
    done = true;
  });

  for (int i = 0; i < timeout_ms / 10; ++i) {
    if (done)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (!done) {
    t.detach(); // abandon
    return std::nullopt;
  }

  t.join();
  return result;
}
inline bool wait_for_daemon_stopped(int timeout_ms = 5000) {
  for (int waited = 0; waited < timeout_ms; waited += 100) {

    auto result =
        call_with_timeout([&] { return run_iss("daemon status --json"); }, 500);

    if (!result) {
      return true;
    }

    auto [exit_code, out] = *result;

    if (exit_code != 0) {
      return true;
    }

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
