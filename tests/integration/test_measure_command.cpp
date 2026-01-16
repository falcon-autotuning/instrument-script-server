#include "instrument-server/Logger.hpp"
#include "instrument-server/server/ServerDaemon.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

// Platform-specific includes
#ifdef _WIN32
#include "instrument-server/compat/WinSock.hpp"
#include <process.h>
#include <windows.h>
#define getpid _getpid
#else
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace instserver;

class MeasureCommandTest : public ::testing::Test {
protected:
  void SetUp() override {
    InstrumentLogger::instance().init("measure_cmd_test. log",
                                      spdlog::level::debug);
    if (ServerDaemon::is_already_running()) {
      int pid = ServerDaemon::get_daemon_pid();

      // Don't kill if PID is ourself or our parent
      int self_pid = getpid();
#ifdef _WIN32
      // Windows doesn't have getppid, just check if it's our own PID
      if (pid != self_pid && pid > 0) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (process) {
          TerminateProcess(process, 0);
          CloseHandle(process);
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
#else
      int parent_pid = getppid();
      if (pid != self_pid && pid != parent_pid && pid > 1) {
        kill(pid, SIGTERM);
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
#endif
    }
    test_dir_ =
        std::filesystem::temp_directory_path() / "instrument_server_test";
    std::filesystem::create_directories(test_dir_);
    auto script_path = test_dir_ / "test.lua";
    std::ofstream script(script_path);
    script << "context:log('Test script running')\n";
    script << "print('Output from script')\n";
    script.close();
  }

  void TearDown() override { std::filesystem::remove_all(test_dir_); }

#ifdef _WIN32
  // Windows implementation using CreateProcess
  int run_command_with_timeout(const std::string &cmd,
                               int timeout_seconds = 5) {
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // Hide console window

    // CreateProcess needs a mutable command line
    std::string mutable_cmd = "cmd.exe /C " + cmd;

    fprintf(stderr, "[DEBUG] Windows parent PID: %d, launching:  %s\n",
            getpid(), cmd.c_str());

    if (!CreateProcessA(nullptr,                  // Application name
                        mutable_cmd.data(),       // Command line (mutable)
                        nullptr,                  // Process attributes
                        nullptr,                  // Thread attributes
                        FALSE,                    // Inherit handles
                        CREATE_NEW_PROCESS_GROUP, // Creation flags (new process
                                                  // group for killing)
                        nullptr,                  // Environment
                        nullptr,                  // Current directory
                        &si,                      // Startup info
                        &pi                       // Process info
                        )) {
      fprintf(stderr, "[DEBUG] CreateProcess failed: %lu\n", GetLastError());
      return -1;
    }

    fprintf(stderr, "[DEBUG] Created process with PID: %lu\n", pi.dwProcessId);

    // Wait for process with timeout
    DWORD timeout_ms = timeout_seconds * 1000;
    DWORD wait_result = WaitForSingleObject(pi.hProcess, timeout_ms);

    int status = -1;
    if (wait_result == WAIT_OBJECT_0) {
      // Process exited normally
      DWORD exit_code;
      if (GetExitCodeProcess(pi.hProcess, &exit_code)) {
        fprintf(stderr, "[DEBUG] Process PID: %lu exited with code: %lu\n",
                pi.dwProcessId, exit_code);
        status = static_cast<int>(exit_code);
      } else {
        fprintf(stderr, "[DEBUG] GetExitCodeProcess failed:  %lu\n",
                GetLastError());
        status = -1;
      }
    } else if (wait_result == WAIT_TIMEOUT) {
      // Timeout - kill the process
      fprintf(stderr, "[DEBUG] Process PID: %lu timed out, terminating.. .\n",
              pi.dwProcessId);

      // Try to terminate gracefully first, then force kill
      TerminateProcess(pi.hProcess, 1);
      WaitForSingleObject(pi.hProcess, 1000); // Wait up to 1s for termination

      LOG_ERROR("TEST", "TIMEOUT", "Command timed out:  {}", cmd);
      status = -1;
    } else {
      fprintf(stderr, "[DEBUG] WaitForSingleObject failed: %lu\n",
              GetLastError());
      status = -1;
    }

    // Cleanup handles
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    fprintf(stderr, "[DEBUG] Returning status: %d\n", status);
    return status;
  }

#else
  // POSIX implementation using fork/exec (original code)
  int run_command_with_timeout(const std::string &cmd,
                               int timeout_seconds = 5) {
    int status = -1;
    pid_t pid = fork();
    if (pid == 0) {
      // Child: start new process group
      if (setpgid(0, 0) < 0) {
        perror("[DEBUG] setpgid failed");
        _exit(127);
      }
      execl("/bin/sh", "sh", "-c", cmd.c_str(), (char *)nullptr);
      perror("[DEBUG] execl failed");
      _exit(127); // exec failed
    } else if (pid > 0) {
      fprintf(stderr, "[DEBUG] Parent PID: %d, forked child PID: %d\n",
              getpid(), pid);
      // Parent: wait with timeout
      auto deadline = std::chrono::steady_clock::now() +
                      std::chrono::seconds(timeout_seconds);
      bool child_exited = false;
      while (std::chrono::steady_clock::now() < deadline) {
        int ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
          fprintf(stderr,
                  "[DEBUG] Parent PID: %d, child PID:  %d exited, status: %d\n",
                  getpid(), pid, status);
          child_exited = true;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      if (!child_exited) {
        fprintf(stderr,
                "[DEBUG] Parent PID: %d, child PID:  %d did not exit in time, "
                "sending SIGTERM to group -%d\n",
                getpid(), pid, pid);
        // Timeout:  kill the whole process group of the child
        int term_result = kill(-pid, SIGTERM);
        if (term_result != 0)
          perror("[DEBUG] kill(SIGTERM) failed");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        int kill_result = kill(-pid, SIGKILL);
        if (kill_result != 0)
          perror("[DEBUG] kill(SIGKILL) failed");
        waitpid(pid, &status, 0); // Clean up zombie
        LOG_ERROR("TEST", "TIMEOUT", "Command timed out:  {}", cmd);
        status = -1;
      } else if (WIFEXITED(status)) {
        fprintf(stderr,
                "[DEBUG] Parent PID: %d, child PID:  %d exited normally, exit "
                "code: %d\n",
                getpid(), pid, WEXITSTATUS(status));
        status = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        fprintf(stderr,
                "[DEBUG] Parent PID: %d, child PID: %d killed by signal: %d\n",
                getpid(), pid, WTERMSIG(status));
        status = -1;
      } else {
        fprintf(
            stderr,
            "[DEBUG] Parent PID: %d, child PID: %d unknown exit status: %d\n",
            getpid(), pid, status);
        status = -1;
      }
    } else {
      perror("[DEBUG] fork failed");
    }
    fprintf(stderr, "[DEBUG] Parent PID: %d, returning status:  %d\n", getpid(),
            status);
    return status;
  }
#endif

  std::filesystem::path test_dir_;
};

TEST_F(MeasureCommandTest, MeasureWithoutDaemon) {
  // Should fail gracefully when daemon not running
  auto script_path = test_dir_ / "test.lua";

  // Use timeout wrapper to prevent hanging
#ifdef _WIN32
  // Windows: redirect to NUL
  std::string cmd =
      "instrument-server measure " + script_path.string() + " > NUL 2>&1";
#else
  // Unix: redirect to /dev/null
  std::string cmd =
      "instrument-server measure " + script_path.string() + " > /dev/null 2>&1";
#endif

  int result = run_command_with_timeout(cmd, 5);

  // Should return non-zero (error) or timeout
  EXPECT_NE(result, 0);
}

TEST_F(MeasureCommandTest, MeasureWithDaemon) {
  // Start daemon
  auto &daemon = ServerDaemon::instance();
  ASSERT_TRUE(daemon.start());

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // Should still fail with no instruments, but shouldn't crash
  auto script_path = test_dir_ / "test.lua";

#ifdef _WIN32
  std::string cmd =
      "instrument-server measure " + script_path.string() + " > NUL 2>&1";
#else
  std::string cmd =
      "instrument-server measure " + script_path.string() + " > /dev/null 2>&1";
#endif

  int result = run_command_with_timeout(cmd, 5);

  daemon.stop();

  // Expect failure (no instruments running)
  EXPECT_NE(result, 0);
}
