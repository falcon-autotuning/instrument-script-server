#pragma once
#include "instrument-script-server/export.h"

#include "instrument-script-server/ipc/PlatformTypes.hpp"
#include "instrument-script-server/ipc/SharedQueue.hpp"
#include "instrument-script-server/server/InstrumentCommand.hpp"
#include "instrument-script-server/server/ParsingTools.hpp"
#include "instrument-script-server/server/SyncCoordinator.hpp"

#include <atomic>
#include <filesystem>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace instserver {
using APICommands = std::unordered_map<std::string, Command>;

/// Proxy for communicating with a worker process via IPC
/// This runs in the main server process
class INSTRUMENT_SERVER_API InstrumentWorkerProxy {
public:
  InstrumentWorkerProxy(std::string instrument_name,
                        std::filesystem::path plugin,
                        std::filesystem::path config);

  ~InstrumentWorkerProxy();

  /// Start worker process and IPC
  bool start();

  /// Stop worker process
  void stop();

  /// Execute command (async, returns future)
  std::future<InstrumentCommandResponse> execute(InstrumentCommand cmd);

  /// Execute command (sync with timeout)
  InstrumentCommandResponse execute_sync(InstrumentCommand cmd,
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

  /// Send BUFFER_ACK message to worker
  void send_buffer_ack(const std::string &buffer_id);
  /// Get expected responses for command
  std::vector<IO> get_responses(const std::string &instrument_name,
                                const std::string &verb) const;

  /// Get expected parameters for command
  std::vector<IO> get_parameters(const std::string &instrument_name,
                                 const std::string &verb) const;

  /// Checks if a command expects a response
  bool command_expects_response(const std::string &verb) const;
  /// Gets responses metedata for a command
  std::vector<IO> get_responses(const std::string &verb) const;
  /// Gets parameters metedata for a command
  std::vector<IO> get_parameters(const std::string &verb) const;

private:
  std::string instrument_name_;
  std::filesystem::path instrument_config_;
  std::filesystem::path plugin_;
  APICommands commands_;

  std::unique_ptr<ipc::SharedQueue> ipc_queue_;
  ProcessId worker_pid_{0};

  // Pending responses (message_id -> promise)
  std::unordered_map<std::string, std::promise<InstrumentCommandResponse>>
      pending_responses_;
  std::unordered_map<std::string, std::vector<ipc::IPCMessage>>
      partial_responses_;
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
  const Command *find_command(const std::string &verb) const;
};

} // namespace instserver
