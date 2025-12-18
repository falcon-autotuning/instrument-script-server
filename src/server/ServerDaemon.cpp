#include "instrument-server/server/ServerDaemon.hpp"
#include "instrument-server/Logger.hpp"
#include <filesystem>
#include <fstream>
#include <signal.h>

#ifdef _WIN32
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

bool ServerDaemon::start() {
  if (running_) {
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

  // Start daemon thread
  running_ = true;
  daemon_thread_ = std::thread([this]() { daemon_loop(); });

  LOG_INFO("DAEMON", "START", "Server daemon started (PID: {})", getpid());

  return true;
}

void ServerDaemon::stop() {
  if (!running_) {
    return;
  }

  LOG_INFO("DAEMON", "STOP", "Stopping server daemon");

  running_ = false;

  // Stop all instruments
  if (registry_) {
    registry_->stop_all();
  }

  // Wait for daemon thread
  if (daemon_thread_.joinable()) {
    daemon_thread_.join();
  }

  // Cleanup
  delete sync_coordinator_;
  sync_coordinator_ = nullptr;

  remove_pid_file();

  LOG_INFO("DAEMON", "STOP", "Server daemon stopped");
}

bool ServerDaemon::is_running() const { return running_; }

void ServerDaemon::daemon_loop() {
  LOG_INFO("DAEMON", "LOOP", "Daemon loop started");

  while (running_) {
    // Heartbeat - keep process alive
    // In future, this could handle IPC commands from clients
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  LOG_INFO("DAEMON", "LOOP", "Daemon loop exited");
}

} // namespace instserver
