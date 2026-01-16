#include "instrument-server/server/ServerDaemon.hpp"
#include "instrument-server/Logger.hpp"
#include "instrument-server/server/HttpRpcServer.hpp"

#include <filesystem>
#include <fstream>
#include <signal.h>

#ifdef _WIN32
#include "instrument-server/compat/WinSock.hpp"
#include <process.h>
#include <windows.h>
#define getpid _getpid
#else
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
  return ".\\instrument-server-runtime";
#else
  // Try XDG_RUNTIME_DIR first, fallback to /tmp
  char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
  if (xdg_runtime) {
    return std::string(xdg_runtime) + "/instrument-server";
  }
  return "/tmp/instrument-server-" +
         std::string(getenv("USER") ? getenv("USER") : "unknown");
#endif
}

ServerDaemon &ServerDaemon::instance() {
  static ServerDaemon daemon;
  return daemon;
}

ServerDaemon::~ServerDaemon() { stop(); }

std::string ServerDaemon::get_pid_file_path() {
  return get_runtime_dir() + "/server.pid";
}

std::string ServerDaemon::get_lock_file_path() {
  return get_runtime_dir() + "/server.lock";
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

  // Create runtime directory if it doesn't exist
  try {
    std::filesystem::create_directories(runtime_dir);
  } catch (const std::exception &e) {
    LOG_ERROR("DAEMON", "INIT", "Failed to create runtime directory:  {}",
              e.what());
    return false;
  }

  std::string pid_file = get_pid_file_path();

  // Write PID to file
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

bool ServerDaemon::is_running() const { return running_.load(); }

void ServerDaemon::daemon_loop() {
  LOG_INFO("DAEMON", "LOOP", "Daemon loop started");

  while (running_.load()) {
    // Heartbeat - keep process alive
    // In future, this could handle IPC commands from clients
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

  // Initialize registry and coordinator
  registry_ = &InstrumentRegistry::instance();
  sync_coordinator_ = new SyncCoordinator();

  // If an RPC port is configured, start RPC server and wait briefly for it to
  // bind.
  if (rpc_port_ > 0) {
    rpc_server_ = new server::HttpRpcServer();
    if (!rpc_server_->start(rpc_port_)) {
      LOG_ERROR("DAEMON", "RPC", "Failed to start RPC server on port {}",
                rpc_port_);
      delete rpc_server_;
      rpc_server_ = nullptr;
      // continue without rpc or fail - choose to fail here so tests relying on
      // RPC get predictable behavior
      remove_pid_file();
      delete sync_coordinator_;
      sync_coordinator_ = nullptr;
      registry_ = nullptr;
      return false;
    }

    // Wait until rpc_server_->port() reports a non-zero bound port or timeout
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

  // Mark running and start daemon thread
  running_.store(true);
  daemon_thread_ = std::thread([this]() { daemon_loop(); });

  LOG_INFO("DAEMON", "START", "Server daemon started (PID: {})", getpid());

  return true;
}

void ServerDaemon::stop() {
  // First, atomically check and clear running flag under lock to prevent races.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_.load()) {
      return;
    }
    LOG_INFO("DAEMON", "STOP", "Stopping server daemon");
    running_.store(false);
  }

  // Stop instruments and allow daemon_loop to exit.
  if (registry_) {
    registry_->stop_all();
  }

  // Stop RPC server if running
  if (rpc_server_) {
    rpc_server_->stop();
    delete rpc_server_;
    rpc_server_ = nullptr;
  }

  // Join the daemon thread outside the mutex to avoid blocking other callers.
  if (daemon_thread_.joinable()) {
    daemon_thread_.join();
  }

  // Cleanup resources under lock
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sync_coordinator_) {
      delete sync_coordinator_;
      sync_coordinator_ = nullptr;
    }
    remove_pid_file();
  }

  LOG_INFO("DAEMON", "STOP", "Server daemon stopped");
}
} // namespace instserver
