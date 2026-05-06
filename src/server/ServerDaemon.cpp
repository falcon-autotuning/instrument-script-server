#include "instrument-script-server/server/ServerDaemon.hpp"
#include "instrument-script-server/Logger.hpp"
#include "instrument-script-server/server/HttpRpcServer.hpp"

#include <csignal>
#include <filesystem>
#include <fstream>
#include <thread>

#ifdef _WIN32
#include "instrument-script-server/compat/WinSock.hpp"
#include <process.h>
#include <windows.h>
#define getpid _getpid
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace instserver {

// Platform-specific paths
static std::string get_runtime_dir() {
#ifdef _WIN32
  char *appdata = getenv("LOCALAPPDATA");
  if (appdata) {
    return std::string(appdata) + "\\InstrumentServer";
  }
  return ".\\instrument-script-server-runtime";
#else
  // Try XDG_RUNTIME_DIR first, fallback to /tmp
  char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
  if (xdg_runtime != nullptr) {
    return std::string(xdg_runtime) + "/instrument-script-server";
  }
  return "/tmp/instrument-script-server-" +
         std::string((getenv("USER") != nullptr) ? getenv("USER") : "unknown");
#endif
}

ServerDaemon &ServerDaemon::instance() {
  static ServerDaemon daemon;
  return daemon;
}

ServerDaemon::~ServerDaemon() {
  // unique_ptr will handle cleanup, but ensure threads are detached
  if (daemon_thread_ && daemon_thread_->joinable()) {
    daemon_thread_->detach();
  }
  if (shutdown_listener_thread_ && shutdown_listener_thread_->joinable()) {
    shutdown_listener_thread_->detach();
  }
  // unique_ptr destructor will delete the thread objects after detach
}

std::string ServerDaemon::get_pid_file_path() {
  return get_runtime_dir() + "/server.pid";
}

std::string ServerDaemon::get_shutdown_pipe_path() {
#ifdef _WIN32
  return "\\\\.\\pipe\\instrument-server-shutdown";
#else
  return get_runtime_dir() + "/shutdown.pipe";
#endif
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
  if (kill(pid, 0) == 0) {
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
  int pid;
  if (!(ifs >> pid)) {
    return -1;
  }

  return pid;
}

bool ServerDaemon::create_pid_file() {
  std::string runtime_dir = get_runtime_dir();

  try {
    std::filesystem::create_directories(runtime_dir);
  } catch (const std::exception &e) {
    LOG_ERROR("DAEMON", "INIT", "Failed to create runtime directory: {}",
              e.what());
    return false;
  }

  std::string pid_file = get_pid_file_path();

  std::ofstream ofs(pid_file);
  if (!ofs) {
    LOG_ERROR("DAEMON", "INIT", "Failed to create PID file: {}", pid_file);
    return false;
  }

  ofs << getpid() << std::endl;
  ofs.close();

  LOG_INFO("DAEMON", "INIT", "Created PID file: {} (PID: {})", pid_file,
           getpid());
  return true;
}

void ServerDaemon::remove_pid_file() {
  std::string pid_file = get_pid_file_path();

  if (std::filesystem::exists(pid_file)) {
    try {
      std::filesystem::remove(pid_file);
      LOG_INFO("DAEMON", "CLEANUP", "Removed PID file");
    } catch (const std::exception &e) {
      LOG_WARN("DAEMON", "CLEANUP", "Failed to remove PID file: {}", e.what());
    }
  }
}

bool ServerDaemon::create_shutdown_pipe() {
#ifdef _WIN32
  // Create named pipe on Windows
  std::string pipe_name = get_shutdown_pipe_path();
  shutdown_pipe_ = CreateNamedPipeA(
      pipe_name.c_str(),
      PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, // Inbound, non-blocking
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

  LOG_INFO("DAEMON", "PIPE", "Created shutdown pipe: {}", pipe_name);
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
    LOG_ERROR("DAEMON", "PIPE", "Failed to create shutdown pipe: {}",
              strerror(errno));
    return false;
  }

  // Open for non-blocking read
  shutdown_pipe_fd_ = open(pipe_path.c_str(), O_RDONLY | O_NONBLOCK);
  if (shutdown_pipe_fd_ < 0) {
    LOG_ERROR("DAEMON", "PIPE", "Failed to open shutdown pipe: {}",
              strerror(errno));
    return false;
  }

  LOG_INFO("DAEMON", "PIPE", "Created shutdown pipe: {}", pipe_path);
  return true;
#endif
}

void ServerDaemon::close_shutdown_pipe() {
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
  // Windows: wait for pipe connection
  while (running_.load()) {
    DWORD bytes_read;
    char buffer[1];

    OVERLAPPED overlapped = {};
    overlapped.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);

    if (!overlapped.hEvent) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    BOOL success =
        ReadFile(shutdown_pipe_, buffer, 1, &bytes_read, &overlapped);

    if (!success) {
      DWORD error = GetLastError();
      if (error == ERROR_IO_PENDING) {
        // Wait up to 100ms for data or handle exit
        DWORD wait_result = WaitForSingleObject(overlapped.hEvent, 100);
        if (wait_result == WAIT_OBJECT_0) {
          // Received shutdown signal
          LOG_INFO("DAEMON", "SHUTDOWN_LISTENER",
                   "Received shutdown signal via pipe");
          running_.store(false);
          break;
        }
        // Timeout, continue loop
      }
    } else {
      // Successfully read
      LOG_INFO("DAEMON", "SHUTDOWN_LISTENER",
               "Received shutdown signal via pipe");
      running_.store(false);
      break;
    }

    CloseHandle(overlapped.hEvent);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
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

void ServerDaemon::signal_shutdown_pipe() {
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
    LOG_WARN("DAEMON", "PIPE", "Failed to open shutdown pipe for writing");
    return;
  }

  if (write(pipe_fd, "X", 1) > 0) {
    LOG_INFO("DAEMON", "PIPE", "Sent shutdown signal via pipe");
  } else {
    LOG_WARN("DAEMON", "PIPE", "Failed to write to shutdown pipe");
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
    LOG_ERROR("DAEMON", "START",
              "Another server instance is already running (PID: {})",
              get_daemon_pid());
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
  registry_ = &InstrumentRegistry::instance();
  sync_coordinator_ = new SyncCoordinator();

  // If an RPC port is configured, start RPC server
  if (rpc_port_ > 0) {
    rpc_server_ = new server::HttpRpcServer();
    if (!rpc_server_->start(rpc_port_)) {
      LOG_ERROR("DAEMON", "RPC", "Failed to start RPC server on port {}",
                rpc_port_);
      delete rpc_server_;
      rpc_server_ = nullptr;
      close_shutdown_pipe();
      remove_pid_file();
      delete sync_coordinator_;
      sync_coordinator_ = nullptr;
      registry_ = nullptr;
      return false;
    }

    auto start_ts = std::chrono::steady_clock::now();
    while (rpc_server_ && rpc_server_->port() == 0) {
      if (std::chrono::steady_clock::now() - start_ts >
          std::chrono::milliseconds(500)) {
        LOG_WARN("DAEMON", "RPC", "RPC server did not bind within timeout");
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    LOG_INFO("DAEMON", "RPC", "RPC server listening on port {}",
             rpc_server_ ? rpc_server_->port() : 0);
  }

  // NO signal registration - rely on pipe only
  // This avoids interfering with gtest or other signal handlers

  // Mark running and start threads
  running_.store(true);
  daemon_thread_ = std::make_unique<std::thread>([this]() { daemon_loop(); });
  shutdown_listener_thread_ =
      std::make_unique<std::thread>([this]() { shutdown_listener_loop(); });

  LOG_INFO("DAEMON", "START", "Server daemon started (PID: {})", getpid());

  return true;
}

void ServerDaemon::stop() {
  // Check if we're in the daemon process BEFORE acquiring lock
  bool is_daemon_process = running_.load();

  if (!is_daemon_process) {
    // We're in a CLI/test process trying to stop the daemon
    int daemon_pid = get_daemon_pid();
    if (daemon_pid > 0) {
      try {
        LOG_INFO("DAEMON", "STOP",
                 "Signaling daemon process (PID: {}) to stop via pipe",
                 daemon_pid);
      } catch (...) {
        // Ignore logging errors
      }

      signal_shutdown_pipe();

      // Give daemon time to shutdown gracefully
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    } else {
      try {
        LOG_WARN("DAEMON", "STOP", "No daemon PID found");
      } catch (...) {
        // Ignore logging errors
      }
    }
    return;
  }

  // We're in the daemon process - do full cleanup
  { // Scope for lock_guard
    std::lock_guard<std::mutex> lock(mutex_);

    if (!running_.load()) {
      return; // Already stopped
    }

    try {
      LOG_INFO("DAEMON", "STOP", "Stopping server daemon (graceful shutdown)");
    } catch (...) {
      // Ignore logging errors during shutdown
    }

    running_.store(false);

    // Stop instruments
    if (registry_) {
      registry_->stop_all();
    }

    // Stop RPC server
    if (rpc_server_) {
      rpc_server_->stop();
      delete rpc_server_;
      rpc_server_ = nullptr;
    }

    // Cleanup resources
    if (sync_coordinator_) {
      delete sync_coordinator_;
      sync_coordinator_ = nullptr;
    }

    remove_pid_file();
    close_shutdown_pipe();
  } // Lock released here

  // Join threads outside the mutex
  try {
    if (daemon_thread_ && daemon_thread_->joinable()) {
      daemon_thread_->join();
    }
  } catch (...) {
    // Suppress
  }

  try {
    if (shutdown_listener_thread_ && shutdown_listener_thread_->joinable()) {
      shutdown_listener_thread_->join();
    }
  } catch (...) {
    // Suppress
  }
}

} // namespace instserver
