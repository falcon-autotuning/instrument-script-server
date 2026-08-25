#include "instrument-script-server/daemon/ServerDaemon.hpp"
#include "instrument-script-server/daemon/GrpcServer.hpp"
#include "instrument-script-server/daemon/InstrumentRegistry.hpp"
#include <cstring>
#include <instrument-log/inst_logging.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include "instrument-script-server/compat/WinSock.hpp"
#include <process.h>
#include <windows.h>
#define getpid _getpid
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
// Shutdown pipe handles (platform-specific)
#ifdef _WIN32
HANDLE shutdown_pipe_{INVALID_HANDLE_VALUE};
#else
int shutdown_pipe_fd_{-1};
#endif
// Platform-specific paths
std::string get_runtime_dir() {
  const char *forced = getenv("INSTRUMENT_SCRIPT_SERVER_RUNTIME_DIR");
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
  // Try XDG_RUNTIME_DIR first, fallback to /tmp
  char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
  if (xdg_runtime != nullptr) {
    return std::string(xdg_runtime) + "/instrument-script-server";
  }
  return "/tmp/instrument-script-server-" +
         std::string((getenv("USER") != nullptr) ? getenv("USER") : "unknown");
#endif
}
} // namespace
namespace instserver {
std::string get_shutdown_pipe_path() {
#ifdef _WIN32
  return "\\\\.\\pipe\\instrument-server-shutdown";
#else
  return get_runtime_dir() + "/shutdown.pipe";
#endif
}

ServerDaemon &ServerDaemon::instance() {
  static ServerDaemon daemon;
  std::string runtime_dir = get_runtime_dir();

#ifdef _WIN32
  SetEnvironmentVariableA("INSTRUMENT_SCRIPT_SERVER_RUNTIME_DIR",
                          runtime_dir.c_str());
#else
  setenv("INSTRUMENT_SCRIPT_SERVER_RUNTIME_DIR", runtime_dir.c_str(), 1);
#endif

  return daemon;
}

ServerDaemon::~ServerDaemon() {
  // unique_ptr will handle cleanup, but ensure threads are detached
  if (shutdown_listener_thread_ && shutdown_listener_thread_->joinable()) {
    shutdown_listener_thread_->join();
  }
  // unique_ptr destructor will delete the thread objects after detach
}

std::string get_pid_file_path() {
  return (std::filesystem::path(get_runtime_dir()) / "server.pid")
      .generic_string();
}

bool ServerDaemon::is_already_running() {
  std::string pid_file = get_pid_file_path();

  if (!std::filesystem::exists(pid_file)) {
    return false;
  }

  // Read PID from file
  std::ifstream ifs(pid_file);
  int pid;
  if (!(ifs >> pid)) {
    return false;
  }

  // Check if process is actually running
#ifdef _WIN32
  HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
  if (process == NULL) {
    // Process doesn't exist, stale PID file
    return false;
  }
  DWORD exit_code;
  GetExitCodeProcess(process, &exit_code);
  CloseHandle(process);
  return exit_code == STILL_ACTIVE;
#else
  // Send signal 0 to check if process exists
  if (kill(pid, 0) == 0 || errno == EPERM) {
    return true;
  }
  return false;
#endif
}

int ServerDaemon::get_daemon_pid() {
  std::string pid_file = get_pid_file_path();

  if (!std::filesystem::exists(pid_file)) {
    return -1;
  }

  std::ifstream ifs(pid_file);
  int pid = 0;
  if (!(ifs >> pid)) {
    return -1;
  }

  return pid;
}

bool create_pid_file() {
  std::string runtime_dir = get_runtime_dir();

  try {
    std::filesystem::create_directories(runtime_dir);
  } catch (const std::exception &e) {
    LOG_ERROR("DAEMON", "INIT", "Failed to create runtime directory: %s",
              e.what());
    return false;
  }

  std::string pid_file = get_pid_file_path();
  std::string tmp_file = pid_file + ".tmp";
  std::string content = std::to_string(getpid()) + "\n";

#ifdef _WIN32

  HANDLE hFile = CreateFileA(tmp_file.c_str(), GENERIC_WRITE,
                             0, // no sharing
                             NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

  if (hFile == INVALID_HANDLE_VALUE) {
    DWORD err = GetLastError();
    LOG_ERROR("DAEMON", "INIT", "CreateFile failed for %s (err=%lu)",
              tmp_file.c_str(), err);
    return false;
  }

  DWORD written = 0;
  BOOL success =
      WriteFile(hFile, content.c_str(), (DWORD)content.size(), &written, NULL);

  if (!success || written != content.size()) {
    DWORD err = GetLastError();
    LOG_ERROR("DAEMON", "INIT", "WriteFile failed (err=%lu)", err);
    CloseHandle(hFile);
    DeleteFileA(tmp_file.c_str());
    return false;
  }

  if (!FlushFileBuffers(hFile)) {
    DWORD err = GetLastError();
    LOG_ERROR("DAEMON", "INIT", "FlushFileBuffers failed (err=%lu)", err);
    CloseHandle(hFile);
    DeleteFileA(tmp_file.c_str());
    return false;
  }

  CloseHandle(hFile);

  if (!MoveFileExA(tmp_file.c_str(), pid_file.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {

    DWORD err = GetLastError();
    LOG_ERROR("DAEMON", "INIT", "MoveFileEx failed (%s → %s) (err=%lu)",
              tmp_file.c_str(), pid_file.c_str(), err);

    DeleteFileA(tmp_file.c_str());
    return false;
  }

#else

  int fd = open(tmp_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    LOG_ERROR("DAEMON", "INIT", "open() failed for %s (errno=%d: %s)",
              tmp_file.c_str(), errno, strerror(errno));
    return false;
  }

  ssize_t total_written = 0;
  while (total_written < (ssize_t)content.size()) {
    ssize_t n = write(fd, content.c_str() + total_written,
                      content.size() - total_written);

    if (n < 0) {
      LOG_ERROR("DAEMON", "INIT", "write() failed (errno=%d: %s)", errno,
                strerror(errno));
      close(fd);
      unlink(tmp_file.c_str());
      return false;
    }
    total_written += n;
  }

  if (fsync(fd) != 0) {
    LOG_ERROR("DAEMON", "INIT", "fsync() failed (errno=%d: %s)", errno,
              strerror(errno));
    close(fd);
    unlink(tmp_file.c_str());
    return false;
  }

  close(fd);

  if (rename(tmp_file.c_str(), pid_file.c_str()) != 0) {
    LOG_ERROR("DAEMON", "INIT", "rename() failed (%s → %s) (errno=%d: %s)",
              tmp_file.c_str(), pid_file.c_str(), errno, strerror(errno));
    unlink(tmp_file.c_str());
    return false;
  }

#endif

  LOG_INFO("DAEMON", "INIT", "Created PID file: %s (PID: %ld)",
           pid_file.c_str(), (long)getpid());

  return true;
}

void remove_pid_file() {
  std::string pid_file = get_pid_file_path();

  if (std::filesystem::exists(pid_file)) {
    try {
      std::filesystem::remove(pid_file);
      LOG_INFO("DAEMON", "CLEANUP", "Removed PID file");
    } catch (const std::exception &e) {
      std::cerr << "Failed to remove PID file: " << e.what() << "\n";
    }
  }
}

bool create_shutdown_pipe() {
#ifdef _WIN32
  // Create named pipe on Windows
  std::string pipe_name = get_shutdown_pipe_path();
  shutdown_pipe_ =
      CreateNamedPipeA(pipe_name.c_str(),
                       PIPE_ACCESS_INBOUND, // Inbound, non-blocking
                       PIPE_TYPE_BYTE | PIPE_WAIT,
                       1, // Max instances
                       0, // Output buffer size
                       0, // Input buffer size
                       0, // Default timeout
                       NULL);

  if (shutdown_pipe_ == INVALID_HANDLE_VALUE) {
    LOG_ERROR("DAEMON", "PIPE", "Failed to create shutdown pipe");
    return false;
  }

  LOG_INFO("DAEMON", "PIPE", "Created shutdown pipe: %s", pipe_name.c_str());
  return true;
#else
  // Create FIFO (named pipe) on Unix
  std::string runtime_dir = get_runtime_dir();
  std::string pipe_path = get_shutdown_pipe_path();

  // Remove old pipe if it exists
  if (std::filesystem::exists(pipe_path)) {
    try {
      std::filesystem::remove(pipe_path);
    } catch (...) {
      // Ignore
    }
  }

  // Create FIFO
  if (mkfifo(pipe_path.c_str(), 0600) != 0 && errno != EEXIST) {
    LOG_ERROR("DAEMON", "PIPE", "Failed to create shutdown pipe: %s",
              strerror(errno));
    return false;
  }

  // Open for non-blocking read
  shutdown_pipe_fd_ = open(pipe_path.c_str(), O_RDONLY | O_NONBLOCK);
  if (shutdown_pipe_fd_ < 0) {
    LOG_ERROR("DAEMON", "PIPE", "Failed to open shutdown pipe: %s",
              strerror(errno));
    return false;
  }

  LOG_INFO("DAEMON", "PIPE", "Created shutdown pipe: %s", pipe_path.c_str());
  return true;
#endif
}

void close_shutdown_pipe() {
#ifdef _WIN32
  if (shutdown_pipe_ != INVALID_HANDLE_VALUE) {
    CloseHandle(shutdown_pipe_);
    shutdown_pipe_ = INVALID_HANDLE_VALUE;
    LOG_INFO("DAEMON", "PIPE", "Closed shutdown pipe");
  }
#else
  if (shutdown_pipe_fd_ >= 0) {
    close(shutdown_pipe_fd_);
    shutdown_pipe_fd_ = -1;

    std::string pipe_path = get_shutdown_pipe_path();
    try {
      std::filesystem::remove(pipe_path);
    } catch (...) {
      // Ignore
    }
    LOG_INFO("DAEMON", "PIPE", "Closed shutdown pipe");
  }
#endif
}

void ServerDaemon::shutdown_listener_loop() {
  LOG_INFO("DAEMON", "SHUTDOWN_LISTENER", "Shutdown listener started");

#ifdef _WIN32
  while (running_.load()) {
    LOG_INFO("DAEMON", "SHUTDOWN_LISTENER",
             "Waiting for client to connect to shutdown pipe...");

    BOOL connected = ConnectNamedPipe(shutdown_pipe_, NULL)
                         ? TRUE
                         : (GetLastError() == ERROR_PIPE_CONNECTED);

    if (!connected) {
      DWORD err = GetLastError();
      LOG_WARN("DAEMON", "SHUTDOWN_LISTENER",
               "ConnectNamedPipe failed (err=%lu), retrying...", err);

      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    LOG_INFO("DAEMON", "SHUTDOWN_LISTENER",
             "Client connected to shutdown pipe");

    char buffer[1];
    DWORD bytes_read = 0;

    BOOL success =
        ReadFile(shutdown_pipe_, buffer, sizeof(buffer), &bytes_read, NULL);

    if (!success) {
      DWORD err = GetLastError();

      LOG_WARN("DAEMON", "SHUTDOWN_LISTENER",
               "ReadFile failed (err=%lu), disconnecting client", err);

      DisconnectNamedPipe(shutdown_pipe_);
      continue;
    }

    if (bytes_read > 0) {
      LOG_INFO("DAEMON", "SHUTDOWN_LISTENER",
               "Received shutdown signal via pipe");

      running_.store(false);

      // Important: disconnect after handling
      DisconnectNamedPipe(shutdown_pipe_);
      break;
    }

    LOG_WARN("DAEMON", "SHUTDOWN_LISTENER",
             "ReadFile returned 0 bytes, disconnecting");

    DisconnectNamedPipe(shutdown_pipe_);
  }
#else
  // Unix: poll the FIFO
  while (running_.load()) {
    char buffer[1];
    ssize_t bytes_read = read(shutdown_pipe_fd_, buffer, 1);

    if (bytes_read > 0) {
      LOG_INFO("DAEMON", "SHUTDOWN_LISTENER",
               "Received shutdown signal via pipe");
      running_.store(false);
      break;
    }

    // No data or error, sleep and retry
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
#endif

  LOG_INFO("DAEMON", "SHUTDOWN_LISTENER", "Shutdown listener exited");
}

void signal_shutdown_pipe() {
  std::string pipe_path = get_shutdown_pipe_path();

#ifdef _WIN32
  // Connect to named pipe on Windows
  HANDLE pipe = CreateFileA(pipe_path.c_str(), GENERIC_WRITE, 0, NULL,
                            OPEN_EXISTING, 0, NULL);

  if (pipe == INVALID_HANDLE_VALUE) {
    LOG_WARN("DAEMON", "PIPE", "Failed to open shutdown pipe for writing");
    return;
  }

  DWORD bytes_written;
  if (WriteFile(pipe, "X", 1, &bytes_written, NULL)) {
    LOG_INFO("DAEMON", "PIPE", "Sent shutdown signal via pipe");
  } else {
    LOG_WARN("DAEMON", "PIPE", "Failed to write to shutdown pipe");
  }

  CloseHandle(pipe);
#else
  // Write to FIFO on Unix
  int pipe_fd = open(pipe_path.c_str(), O_WRONLY | O_NONBLOCK);
  if (pipe_fd < 0) {
    std::cerr << "Failed to open shutdown pipe for writing" << "\n";
    return;
  }

  if (write(pipe_fd, "X", 1) > 0) {
    LOG_INFO("DAEMON", "PIPE", "Sent shutdown signal via pipe");
  } else {
    std::cerr << "Failed to write to shutdown pipe" << "\n";
  }

  close(pipe_fd);
#endif
}

bool ServerDaemon::is_running() const { return running_.load(); }

void ServerDaemon::daemon_loop() {
  LOG_INFO("DAEMON", "LOOP", "Daemon loop started");

  while (running_.load()) {
    // Heartbeat - keep process alive
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  LOG_INFO("DAEMON", "LOOP", "Daemon loop exited");
}
bool ServerDaemon::start() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (running_.load()) {
    LOG_WARN("DAEMON", "START", "Daemon already running");
    return true;
  }

  // Check if another instance is running
  if (is_already_running()) {
    LOG_WARN("DAEMON", "START",
             "Another server instance is already running (PID: %ld)",
             (long)get_daemon_pid());
    return false;
  }

  LOG_INFO("DAEMON", "START", "Starting server daemon");

  // Create PID file
  if (!create_pid_file()) {
    return false;
  }

  // Create shutdown pipe
  if (!create_shutdown_pipe()) {
    remove_pid_file();
    return false;
  }

  // Initialize registry and coordinator
  daemon::InstrumentRegistry::instance();
  sync_coordinator_ = std::make_unique<SyncCoordinator>();

  // If an RPC port is configured, start RPC server
  if (rpc_port_ > 0) {
    rpc_server_ = new daemon::GrpcServer();
    if (!rpc_server_->start(rpc_port_)) {
      LOG_ERROR("DAEMON", "RPC", "Failed to start RPC server on port %d",
                rpc_port_);
      delete rpc_server_;
      rpc_server_ = nullptr;
      close_shutdown_pipe();
      remove_pid_file();
      sync_coordinator_ = nullptr;
      return false;
    }

    auto start_ts = std::chrono::steady_clock::now();
    while ((rpc_server_ != nullptr) && rpc_server_->port() == 0) {
      if (std::chrono::steady_clock::now() - start_ts >
          std::chrono::milliseconds(500)) {
        LOG_WARN("DAEMON", "RPC", "RPC server did not bind within timeout");
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    LOG_INFO("DAEMON", "RPC", "RPC server listening on port %d",
             rpc_server_ ? rpc_server_->port() : 0);
  }

  // NO signal registration - rely on pipe only
  // This avoids interfering with gtest or other signal handlers

  // Mark running and start threads
  running_.store(true);
  std::promise<void> ready;
  auto future = ready.get_future();

  shutdown_listener_thread_ =
      std::make_unique<std::thread>([this, p = std::move(ready)]() mutable {
        p.set_value();

        shutdown_listener_loop();
      });

  future.wait();

  LOG_INFO("DAEMON", "START", "Server daemon started (PID: %ld)",
           (long)getpid());

  return true;
}
#ifdef _WIN32
void wake_shutdown_listener() {
  LOG_DEBUG("DAEMON", "PIPE", "wake_shutdown_listener enter");

  HANDLE hPipe = CreateFileA(get_shutdown_pipe_path().c_str(), GENERIC_WRITE, 0,
                             nullptr, OPEN_EXISTING, 0, nullptr);

  if (hPipe == INVALID_HANDLE_VALUE) {
    LOG_DEBUG("DAEMON", "PIPE", "wake_shutdown_listener failed err=%lu",
              GetLastError());
    return;
  }

  LOG_DEBUG("DAEMON", "PIPE", "wake_shutdown_listener connected");

  DWORD written = 0;
  char signal = 1;

  WriteFile(hPipe, &signal, 1, &written, nullptr);

  LOG_DEBUG("DAEMON", "PIPE", "wake_shutdown_listener wrote");

  CloseHandle(hPipe);

  LOG_DEBUG("DAEMON", "PIPE", "wake_shutdown_listener exit");
}
#endif
void ServerDaemon::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    LOG_INFO("DAEMON", "STOP", "Stopping server daemon");
    // Signal threads to exit first.
    running_.store(false);

    // Remove the PID file BEFORE stopping the gRPC server so that by the
    // time the gRPC port goes silent (and callers see the daemon as "stopped"),
    // any subsequent `daemon start` will NOT find a stale PID file and will
    // be able to proceed immediately. This eliminates the race between
    // wait_for_daemon_stopped() returning true and the PID file being gone.
    remove_pid_file();

    // Stop instruments and RPC server while still holding the lock.
    daemon::InstrumentRegistry::instance().stop_all();

    if (rpc_server_ != nullptr) {
      rpc_server_->stop();
      delete rpc_server_;
      rpc_server_ = nullptr;
    }
  }
  LOG_DEBUG("DAEMON", "STOP", "RPC server stopped");

  if (shutdown_listener_thread_ && shutdown_listener_thread_->joinable()) {
#ifdef _WIN32
    wake_shutdown_listener();
#endif
    shutdown_listener_thread_->join();
  }

  close_shutdown_pipe();

  inst_log_shutdown();
}
} // namespace instserver
