#include "instrument-script-server/ipc/ProcessManager.hpp"
#include <instrument-log/inst_logging.h>

#include <filesystem>

#ifdef _WIN32
#include <processthreadsapi.h>
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#endif
namespace {
std::filesystem::path get_executable_dir() {
  std::vector<char> buffer(INITIAL_PATH_BUFFER_SIZE);
  auto try_get_path = [&](char *data, size_t size) -> size_t {
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(nullptr, data, static_cast<DWORD>(size));
    return len == 0 ? 0 : static_cast<size_t>(len);
#else
    ssize_t count = readlink("/proc/self/exe", data, size);
    return count == -1 ? 0 : static_cast<size_t>(count);
#endif
  };
  while (true) {
    size_t length = try_get_path(buffer.data(), buffer.size());
    if (length == 0) {
      return {};
    }
    if (length < buffer.size()) {
      return std::filesystem::path(std::string_view(buffer.data(), length))
          .parent_path();
    }
    buffer.resize(buffer.size() * 2);
  }
}

bool is_alive_impl(ProcessHandle handle) {
#ifdef _WIN32
  DWORD exit_code;
  if (!GetExitCodeProcess(handle, &exit_code)) {
    return false;
  }
  return exit_code == STILL_ACTIVE;
#else
  int status = 0;
  pid_t result = waitpid(handle, &status, WNOHANG);

  if (result == 0) {
    return true; // still running
  }

  if (result == handle) {
    return false; // exited (and reaped)
  }

  if (result == -1) {
    if (errno == ECHILD) {
      return false; // already gone
    }
  }

  return false;

#endif
}
} // namespace
namespace instserver::ipc {

ProcessManager::~ProcessManager() { cleanup_all(); }

ProcessId
ProcessManager::spawn_worker(const std::filesystem::path &instrument_config,
                             const std::filesystem::path &plugin,
                             const std::string &worker_executable) {
  LOG_INFO("PROCESS", "SPAWN",
           "Spawning worker for instrument with config: %s with plugin: %s",
           instrument_config.string().c_str(), plugin.string().c_str());

  // Resolve executable path
  std::string resolved_worker = worker_executable;
  std::filesystem::path exe_dir = get_executable_dir();

  if (!exe_dir.empty()) {
    const std::vector<std::filesystem::path> candidates = {
        exe_dir / worker_executable, exe_dir.parent_path() / worker_executable};

    for (const auto &candidate : candidates) {
      if (std::filesystem::exists(candidate)) {
        resolved_worker = candidate.string();
        break;
      }
    }
  }

  std::vector<std::string> args = {resolved_worker, instrument_config.string(),
                                   plugin.string()};

  std::string instrument_name;
  YAML::Node doc;
  try {
    YAML::Node doc = YAML::LoadFile(instrument_config.string());
    if (!doc["name"]) {
      throw std::runtime_error("Missing required field: name");
    }
    instrument_name = doc["name"].as<std::string>();
  } catch (const std::exception &e) {
    LOG_ERROR("CONFIG", "WORKER_MAIN", "Invalid config '%s': %s",
              instrument_config.c_str(), e.what());
    return 1;
  }
  ProcessId pid = spawn_process_impl(args);
  if (pid == 0) {
    LOG_ERROR("PROCESS", "SPAWN", "Failed to spawn worker for: %s",
              instrument_name.c_str());
    return 0;
  }

  // Common process info setup
  auto now = std::chrono::steady_clock::now();
  auto last_heartbeat = now.time_since_epoch().count();

  {
    std::lock_guard lock(mutex_);
    auto &entry = processes_[pid];
    if (!entry) {
      entry = std::make_unique<ProcessInfo>();
    }
    entry->pid = pid;
    entry->instrument_name = instrument_name;
    entry->plugin_path = plugin.string();
    entry->started_at = now;
    entry->is_alive = true;
    entry->last_heartbeat = last_heartbeat;
#ifndef _WIN32
    entry->handle = pid;
#endif
  }

  LOG_INFO("PROCESS", "SPAWN", "Worker spawned successfully: PID=%ld",
           static_cast<long>(pid));

  return pid;
}

bool ProcessManager::is_alive(ProcessId pid) const {
  std::lock_guard lock(mutex_);
  auto loc = processes_.find(pid);
  if (loc == processes_.end()) {
    return false;
  }

  return is_alive_impl(loc->second->handle);
}

bool ProcessManager::kill_process(ProcessId pid, bool force) {
  std::lock_guard lock(mutex_);
  auto loc = processes_.find(pid);
  if (loc == processes_.end()) {
    return false;
  }
  LOG_INFO("PROCESS", "KILL", "Killing process: PID=%d (force=%s)", pid,
           force ? "true" : "false");
#ifdef _WIN32
  bool result = TerminateProcess(loc->second->handle, force ? 1 : 0) != 0;
#else
  bool result = kill(loc->second->handle, force ? SIGKILL : SIGTERM) == 0;
#endif
  if (result) {
    loc->second->is_alive = false;
  }
  return result;
}

const ProcessManager::ProcessInfo *
ProcessManager::get_process_info(ProcessId pid) const {
  std::lock_guard lock(mutex_);
  auto it = processes_.find(pid);
  if (it == processes_.end()) {
    return nullptr;
  }
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

bool ProcessManager::wait_for_exit(ProcessId pid,
                                   std::chrono::milliseconds timeout) const {
#ifdef _WIN32
  ProcessHandle hProcess = OpenProcess(SYNCHRONIZE, FALSE, pid);
  if (hProcess == nullptr) {
    // Process likely already exited
    return true;
  }

  DWORD result =
      WaitForSingleObject(hProcess, static_cast<DWORD>(timeout.count()));

  CloseHandle(hProcess);

  return result == WAIT_OBJECT_0;

#else
  auto deadline = std::chrono::steady_clock::now() + timeout;

  while (std::chrono::steady_clock::now() < deadline) {
    int status = 0;
    pid_t result = waitpid(pid, &status, WNOHANG);

    if (result == pid) {
      return true;
    }

    if (result == -1 && errno == ECHILD) {
      return true;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(PROCESS_KILL_POLL_INTERVAL_MS));
  }

  return false;
#endif
}
void ProcessManager::cleanup_all() {
  LOG_INFO("PROCESS", "CLEANUP", "Cleaning up all worker processes");
  std::vector<ProcessId> pids;
  {
    std::lock_guard lock(mutex_);
    pids.reserve(processes_.size());
    for (const auto &[pid, _] : processes_) {
      pids.push_back(pid);
    }
  }
  for (ProcessId pid : pids) {
    kill_process(pid, true);
  }
  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(PROCESS_KILL_TIMEOUT_MS);

  while (std::chrono::steady_clock::now() < deadline) {
    bool all_exited = true;
    for (ProcessId pid : pids) {
      if (is_alive(pid)) {
        all_exited = false;
        break;
      }
    }
    if (all_exited) {
      break;
    }
    std::this_thread::sleep_for(
        std::chrono::milliseconds(PROCESS_KILL_POLL_INTERVAL_MS));
  }

#ifdef _WIN32
  std::vector<ProcessHandle> handles;

  {
    std::lock_guard lock(mutex_);
    handles.reserve(processes_.size());
    for (auto &[pid, info] : processes_) {
      if (info->handle != nullptr) {
        handles.push_back(info->handle);
      }
    }
  }

  for (auto handle : handles) {
    CloseHandle(handle);
  }
#endif

  {
    std::lock_guard lock(mutex_);
    processes_.clear();
  }
}

void ProcessManager::start_heartbeat_monitor(
    std::chrono::milliseconds interval,
    std::function<void(ProcessId)> on_dead_callback) {
  if (monitor_running_) {
    return;
  }

  dead_callback_ = std::move(on_dead_callback);
  heartbeat_timeout_ = interval * 2; // Allow 2 missed heartbeats
  monitor_running_ = true;

  monitor_thread_ = std::thread([this]() { heartbeat_monitor_loop(); });

  LOG_INFO("PROCESS", "MONITOR", "Started heartbeat monitor");
}

void ProcessManager::stop_heartbeat_monitor() {
  if (!monitor_running_) {
    return;
  }
  monitor_running_ = false;
  if (monitor_thread_.joinable()) {
    monitor_thread_.join();
  }
  LOG_INFO("PROCESS", "MONITOR", "Stopped heartbeat monitor");
}

void ProcessManager::update_heartbeat(ProcessId pid) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  std::lock_guard lock(mutex_);
  auto loc = processes_.find(pid);
  if (loc != processes_.end()) {
    loc->second->last_heartbeat = now;
  }
}

void ProcessManager::heartbeat_monitor_loop() {
  struct DeadInfo {
    ProcessId pid;
    ProcessHandle handle;
    long long elapsed_ms;
  };
  using namespace std::chrono;
  while (monitor_running_) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(HEARTBEAT_TIMEOUT_MS));
    const auto now = steady_clock::now();
    std::vector<DeadInfo> dead_candidates;
    {
      std::lock_guard lock(mutex_);

      for (auto &[pid, info] : processes_) {
        const auto last =
            steady_clock::time_point(nanoseconds(info->last_heartbeat.load()));
        const auto elapsed = now - last;
        if (elapsed <= heartbeat_timeout_) {
          continue;
        }
        dead_candidates.push_back(
            {pid, info->handle, duration_cast<milliseconds>(elapsed).count()});
      }
    }
    std::vector<ProcessId> confirmed_dead;
    for (const auto &d : dead_candidates) {
      LOG_WARN("PROCESS", "HEARTBEAT", "Process %d missed heartbeat (%lld ms)",
               d.pid, d.elapsed_ms);
      if (!is_alive_impl(d.handle)) {
        confirmed_dead.push_back(d.pid);
      }
    }
    {
      std::lock_guard lock(mutex_);
      for (ProcessId pid : confirmed_dead) {
        auto it = processes_.find(pid);
        if (it != processes_.end()) {
          it->second->is_alive = false;
        }
      }
    }
    for (ProcessId pid : confirmed_dead) {
      LOG_ERROR("PROCESS", "DEAD", "Worker process died: PID=%d", pid);

      if (dead_callback_) {
        dead_callback_(pid);
      }
    }
  }
}

ProcessId
ProcessManager::spawn_process_impl(const std::vector<std::string> &args) {
  ProcessId pid = 0;
  ProcessHandle handle{};

#ifdef _WIN32
  // Build command line (Windows)
  std::ostringstream cmdline;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i > 0)
      cmdline << ' ';
    cmdline << '"' << args[i] << '"';
  }

  std::string cmd = cmdline.str();

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};

  if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                      nullptr, &si, &pi)) {
    LOG_ERROR("PROCESS", "SPAWN", "CreateProcess failed: {}", GetLastError());
    return 0;
  }

  CloseHandle(pi.hThread);

  pid = static_cast<ProcessId>(pi.dwProcessId);
  handle = pi.hProcess;

#else
  // Build argv (POSIX)
  std::vector<std::string> args_copy = args;
  std::vector<char *> argv;
  argv.reserve(args_copy.size() + 1);

  for (auto &arg : args_copy) {
    argv.push_back(arg.data());
  }
  argv.push_back(nullptr);

  pid_t child_pid = 0;
  int status =
      posix_spawnp(&child_pid, argv[0], nullptr, nullptr, argv.data(), environ);

  if (status != 0) {
    LOG_ERROR("PROCESS", "SPAWN", "posix_spawn failed: {}", strerror(status));
    return 0;
  }

  pid = static_cast<ProcessId>(child_pid);
  handle = static_cast<ProcessHandle>(child_pid);
#endif

  auto now = std::chrono::steady_clock::now();

  {
    std::lock_guard lock(mutex_);

    auto &entry = processes_[pid];
    if (!entry) {
      entry = std::make_unique<ProcessInfo>();
    }

    entry->pid = pid;
    entry->handle = handle;
    entry->is_alive = true;
    entry->last_heartbeat = now.time_since_epoch().count();
  }

  return pid;
}

} // namespace instserver::ipc
