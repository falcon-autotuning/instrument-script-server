#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace instserver {
namespace ipc {

#ifdef _WIN32
#include <windows.h>
using ProcessHandle = HANDLE;
using ProcessId = DWORD;
#else
#include <sys/types.h>
using ProcessHandle = pid_t;
using ProcessId = pid_t;
#endif

/// Manages worker process lifecycle
class ProcessManager {
public:
  struct ProcessInfo {
    ProcessId pid;
    ProcessHandle handle;
    std::string instrument_name;
    std::string plugin_path;
    std::chrono::steady_clock::time_point started_at;
    std::atomic<bool> is_alive{true};
    std::atomic<uint64_t> last_heartbeat{0};
  };

  ProcessManager() = default;
  ~ProcessManager();

  /// Spawn worker process
  /// Returns process ID on success, 0 on failure
  ProcessId
  spawn_worker(const std::string &instrument_name,
               const std::string &plugin_path,
               const std::string &worker_executable = "instrument-worker");

  /// Check if process is alive
  bool is_alive(ProcessId pid) const;

  /// Kill process
  bool kill_process(ProcessId pid, bool force = false);

  /// Wait for process to exit (with timeout)
  bool wait_for_exit(ProcessId pid, std::chrono::milliseconds timeout);

  /// Get process info
  const ProcessInfo *get_process_info(ProcessId pid) const;

  /// List all managed processes
  std::vector<ProcessId> list_processes() const;

  /// Cleanup all processes
  void cleanup_all();

  /// Start heartbeat monitor thread
  void start_heartbeat_monitor(std::chrono::milliseconds interval,
                               std::function<void(ProcessId)> on_dead_callback);

  /// Stop heartbeat monitor
  void stop_heartbeat_monitor();

  /// Update heartbeat timestamp
  void update_heartbeat(ProcessId pid);

private:
  std::unordered_map<ProcessId, std::unique_ptr<ProcessInfo>> processes_;
  mutable std::mutex mutex_;

  // Heartbeat monitoring
  std::atomic<bool> monitor_running_{false};
  std::thread monitor_thread_;
  std::chrono::milliseconds heartbeat_timeout_{10000}; // 10s
  std::function<void(ProcessId)> dead_callback_;

  void heartbeat_monitor_loop();

  // Platform-specific helpers
  ProcessId spawn_process_impl(const std::vector<std::string> &args);
  bool kill_process_impl(ProcessHandle handle, bool force);
  bool is_alive_impl(ProcessHandle handle) const;
};

} // namespace ipc
} // namespace instserver
