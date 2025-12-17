#include "instrument_script/ipc/ProcessManager.hpp"
#include "instrument_script/Logger.hpp"
#include <sstream>

#ifdef _WIN32
#include <processthreadsapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace instrument_script {
namespace ipc {

ProcessManager::~ProcessManager() { cleanup_all(); }

ProcessId ProcessManager::spawn_worker(const std::string &instrument_name,
                                       const std::string &plugin_path,
                                       const std::string &worker_executable) {
  LOG_INFO("PROCESS", "SPAWN",
           "Spawning worker for instrument:  {} with plugin: {}",
           instrument_name, plugin_path);

  std::vector<std::string> args = {worker_executable, "--instrument",
                                   instrument_name, "--plugin", plugin_path};

  ProcessId pid = spawn_process_impl(args);

  if (pid == 0) {
    LOG_ERROR("PROCESS", "SPAWN", "Failed to spawn worker for:  {}",
              instrument_name);
    return 0;
  }

  // Store process info
  auto info = std::make_unique<ProcessInfo>();
  info->pid = pid;
  info->handle = pid; // On Unix, pid is the handle
  info->instrument_name = instrument_name;
  info->plugin_path = plugin_path;
  info->started_at = std::chrono::steady_clock::now();
  info->is_alive = true;
  info->last_heartbeat =
      std::chrono::steady_clock::now().time_since_epoch().count();

  {
    std::lock_guard lock(mutex_);
    processes_[pid] = std::move(info);
  }

  LOG_INFO("PROCESS", "SPAWN", "Worker spawned successfully:  PID={}", pid);

  return pid;
}

bool ProcessManager::is_alive(ProcessId pid) const {
  std::lock_guard lock(mutex_);
  auto it = processes_.find(pid);
  if (it == processes_.end())
    return false;

  return is_alive_impl(it->second->handle);
}

bool ProcessManager::kill_process(ProcessId pid, bool force) {
  std::lock_guard lock(mutex_);
  auto it = processes_.find(pid);
  if (it == processes_.end())
    return false;

  LOG_INFO("PROCESS", "KILL", "Killing process: PID={} (force={})", pid, force);

  bool result = kill_process_impl(it->second->handle, force);

  if (result) {
    it->second->is_alive = false;
  }

  return result;
}

bool ProcessManager::wait_for_exit(ProcessId pid,
                                   std::chrono::milliseconds timeout) {
  auto deadline = std::chrono::steady_clock::now() + timeout;

  while (std::chrono::steady_clock::now() < deadline) {
    if (!is_alive(pid)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return false;
}

const ProcessManager::ProcessInfo *
ProcessManager::get_process_info(ProcessId pid) const {
  std::lock_guard lock(mutex_);
  auto it = processes_.find(pid);
  if (it == processes_.end())
    return nullptr;
  return it->second.get();
}

std::vector<ProcessId> ProcessManager::list_processes() const {
  std::lock_guard lock(mutex_);
  std::vector<ProcessId> pids;
  pids.reserve(processes_.size());
  for (const auto &[pid, _] : processes_) {
    pids.push_back(pid);
  }
  return pids;
}

void ProcessManager::cleanup_all() {
  LOG_INFO("PROCESS", "CLEANUP", "Cleaning up all worker processes");

  std::vector<ProcessId> pids;
  {
    std::lock_guard lock(mutex_);
    for (const auto &[pid, _] : processes_) {
      pids.push_back(pid);
    }
  }

  for (ProcessId pid : pids) {
    kill_process(pid, true);
    wait_for_exit(pid, std::chrono::milliseconds(2000));
  }

  processes_.clear();
}

void ProcessManager::start_heartbeat_monitor(
    std::chrono::milliseconds interval,
    std::function<void(ProcessId)> on_dead_callback) {
  if (monitor_running_)
    return;

  dead_callback_ = std::move(on_dead_callback);
  heartbeat_timeout_ = interval * 2; // Allow 2 missed heartbeats
  monitor_running_ = true;

  monitor_thread_ = std::thread([this]() { heartbeat_monitor_loop(); });

  LOG_INFO("PROCESS", "MONITOR", "Started heartbeat monitor");
}

void ProcessManager::stop_heartbeat_monitor() {
  if (!monitor_running_)
    return;

  monitor_running_ = false;
  if (monitor_thread_.joinable()) {
    monitor_thread_.join();
  }

  LOG_INFO("PROCESS", "MONITOR", "Stopped heartbeat monitor");
}

void ProcessManager::update_heartbeat(ProcessId pid) {
  std::lock_guard lock(mutex_);
  auto it = processes_.find(pid);
  if (it != processes_.end()) {
    it->second->last_heartbeat =
        std::chrono::steady_clock::now().time_since_epoch().count();
  }
}

void ProcessManager::heartbeat_monitor_loop() {
  while (monitor_running_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::vector<ProcessId> dead_pids;

    {
      std::lock_guard lock(mutex_);
      for (auto &[pid, info] : processes_) {
        auto elapsed = now - info->last_heartbeat.load();
        auto elapsed_ms = std::chrono::nanoseconds(elapsed);

        if (elapsed_ms > heartbeat_timeout_) {
          LOG_WARN(
              "PROCESS", "HEARTBEAT", "Process {} missed heartbeat ({}ms)", pid,
              std::chrono::duration_cast<std::chrono::milliseconds>(elapsed_ms)
                  .count());

          if (!is_alive_impl(info->handle)) {
            dead_pids.push_back(pid);
            info->is_alive = false;
          }
        }
      }
    }

    // Notify callback of dead processes
    for (ProcessId pid : dead_pids) {
      LOG_ERROR("PROCESS", "DEAD", "Worker process died: PID={}", pid);
      if (dead_callback_) {
        dead_callback_(pid);
      }
    }
  }
}

// Platform-specific implementations

#ifdef _WIN32

ProcessId
ProcessManager::spawn_process_impl(const std::vector<std::string> &args) {
  std::ostringstream cmdline;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i > 0)
      cmdline << " ";
    cmdline << "\"" << args[i] << "\"";
  }

  STARTUPINFOA si = {sizeof(si)};
  PROCESS_INFORMATION pi = {};

  std::string cmdline_str = cmdline.str();

  if (!CreateProcessA(nullptr, const_cast<char *>(cmdline_str.c_str()), nullptr,
                      nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    LOG_ERROR("PROCESS", "SPAWN", "CreateProcess failed: {}", GetLastError());
    return 0;
  }

  CloseHandle(pi.hThread); // Don't need thread handle

  return pi.dwProcessId;
}

bool ProcessManager::kill_process_impl(ProcessHandle handle, bool force) {
  UINT exit_code = force ? 1 : 0;
  return TerminateProcess(handle, exit_code) != 0;
}

bool ProcessManager::is_alive_impl(ProcessHandle handle) const {
  DWORD exit_code;
  if (!GetExitCodeProcess(handle, &exit_code)) {
    return false;
  }
  return exit_code == STILL_ACTIVE;
}

#else // POSIX

ProcessId
ProcessManager::spawn_process_impl(const std::vector<std::string> &args) {
  std::vector<char *> argv;
  for (const auto &arg : args) {
    argv.push_back(const_cast<char *>(arg.c_str()));
  }
  argv.push_back(nullptr);

  pid_t pid;
  int status =
      posix_spawn(&pid, argv[0], nullptr, nullptr, argv.data(), environ);

  if (status != 0) {
    LOG_ERROR("PROCESS", "SPAWN", "posix_spawn failed: {}", strerror(status));
    return 0;
  }

  return pid;
}

bool ProcessManager::kill_process_impl(ProcessHandle handle, bool force) {
  int signal = force ? SIGKILL : SIGTERM;
  return kill(handle, signal) == 0;
}

bool ProcessManager::is_alive_impl(ProcessHandle handle) const {
  int status;
  pid_t result = waitpid(handle, &status, WNOHANG);
  return result == 0; // 0 means still running
}

#endif

} // namespace ipc
} // namespace instrument_script
