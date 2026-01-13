#pragma once
#include "instrument-server/SerializedCommand.hpp"
#include "instrument-server/ipc/ProcessManager.hpp"
#include "instrument-server/ipc/SharedQueue.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"
#include <atomic>
#include <future>
#include <mutex>
#include <string>
#include <thread>

namespace instserver {

/// Proxy for communicating with a worker process via IPC
/// This runs in the main server process
class InstrumentWorkerProxy {
public:
  InstrumentWorkerProxy(const std::string &instrument_name,
                        const std::string &plugin_path,
                        const std::string &config_json,
                        const std::string &api_def_json,
                        SyncCoordinator &sync_coordinator);

  ~InstrumentWorkerProxy();

  /// Start worker process and IPC
  bool start();

  /// Stop worker process
  void stop();

  /// Execute command (async, returns future)
  std::future<CommandResponse> execute(SerializedCommand cmd);

  /// Execute command (sync with timeout)
  CommandResponse execute_sync(SerializedCommand cmd,
                               std::chrono::milliseconds timeout);

  /// Check if worker is alive
  bool is_alive() const;

  /// Get instrument name
  const std::string &name() const { return instrument_name_; }

  /// Get statistics
  struct Stats {
    uint64_t commands_sent{0};
    uint64_t commands_completed{0};
    uint64_t commands_failed{0};
    uint64_t commands_timeout{0};
  };
  Stats get_stats() const;

  /// Send SYNC_CONTINUE message to worker
  void send_sync_continue(uint64_t sync_token);

private:
  std::string instrument_name_;
  std::string plugin_path_;
  std::string config_json_;  // JSON as string
  std::string api_def_json_; // JSON as string
  SyncCoordinator &sync_coordinator_;

  std::unique_ptr<ipc::SharedQueue> ipc_queue_;
  ProcessId worker_pid_{0};

  // Pending responses (message_id -> promise)
  std::unordered_map<uint64_t, std::promise<CommandResponse>>
      pending_responses_;
  std::mutex pending_mutex_;

  // Response listener thread
  std::thread response_thread_;
  std::atomic<bool> running_{false};

  // Stats
  mutable std::mutex stats_mutex_;
  Stats stats_;

  // Message ID counter
  std::atomic<uint64_t> next_message_id_{1};

  void response_listener_loop();
  void handle_worker_death();
  void send_shutdown_message();
  void stop_worker_process();
  void join_response_thread_with_timeout();
  void cleanup_pending_promises();
  void cleanup_ipc();
  void handle_ipc_message(const ipc::IPCMessage &msg);
  void handle_response_message(const ipc::IPCMessage &msg);
  void handle_sync_ack_message(const ipc::IPCMessage &msg);
};

} // namespace instserver
