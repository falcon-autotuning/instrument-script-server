#include "instrument-script-server/server/InstrumentWorkerProxy.hpp"
#include "instrument-script-server/ipc/ProcessManager.hpp"
#include "instrument-script-server/ipc/SharedQueue.hpp"
#include <instrument-log/inst_logging.h>

namespace instserver {

// Global process manager instance.
//
// Use a heap-backed singleton to avoid static deinitialization order issues at
// process shutdown. InstrumentRegistry teardown can still call into the process
// manager during global destruction; a function-local static object can be
// destroyed too early, leading to use-after-destruction in stop paths.
static ipc::ProcessManager &get_process_manager() {
  static auto *manager = new ipc::ProcessManager();
  return *manager;
}

InstrumentWorkerProxy::InstrumentWorkerProxy(const std::string &instrument_name,
                                             const std::string &plugin_path,
                                             const std::string &config_json,
                                             const std::string &api_def_json,
                                             SyncCoordinator &sync_coordinator)
    : instrument_name_(instrument_name), plugin_path_(plugin_path),
      config_json_(config_json), api_def_json_(api_def_json),
      sync_coordinator_(sync_coordinator) {}

InstrumentWorkerProxy::~InstrumentWorkerProxy() { stop(); }

bool InstrumentWorkerProxy::start() {
  LOG_INFO(instrument_name_.c_str(), "PROXY", "Starting worker proxy");

  // Create IPC queues
  try {
    ipc_queue_ = ipc::SharedQueue::create_server_queue(instrument_name_);
  } catch (const std::exception &ex) {
    LOG_ERROR(instrument_name_.c_str(), "PROXY",
              "Failed to create IPC queues: {}", ex.what());
    return false;
  }

  // Spawn worker process
  worker_pid_ =
      get_process_manager().spawn_worker(instrument_name_, plugin_path_);

  if (worker_pid_ == 0) {
    LOG_ERROR(instrument_name_.c_str(), "PROXY",
              "Failed to spawn worker process");
    return false;
  }

  LOG_INFO(instrument_name_.c_str(), "PROXY",
           "Worker process spawned:  PID=%ld", (long)worker_pid_);

  // Start response listener thread
  running_ = true;
  response_thread_ = std::thread([this]() { response_listener_loop(); });

  if (!is_alive()) {
    LOG_ERROR(instrument_name_.c_str(), "PROXY", "Worker died during startup");
    stop();
    return false;
  }

  LOG_INFO(instrument_name_.c_str(), "PROXY",
           "Worker proxy started successfully");
  return true;
}

void InstrumentWorkerProxy::stop() {
  if (!running_.exchange(false)) {
    return;
  }

  LOG_INFO(instrument_name_.c_str(), "PROXY", "Stopping worker proxy");

  send_shutdown_message();
  if (ipc_queue_ && ipc_queue_->is_valid()) {
    ipc::IPCMessage wake_msg;
    wake_msg.type = ipc::IPCMessage::Type::HEARTBEAT;
    wake_msg.id = 0;
    wake_msg.sync_token = 0;
    wake_msg.payload_size = 0;

    ipc_queue_->send_to_response_queue(wake_msg, std::chrono::milliseconds(50));
  }

  if (worker_pid_ != 0) {
    if (!get_process_manager().wait_for_exit(worker_pid_,
                                             std::chrono::milliseconds(200))) {
      LOG_WARN(instrument_name_.c_str(), "PROXY",
               "Worker did not exit gracefully, forcing kill");
      get_process_manager().kill_process(worker_pid_, true);
    }
  }
  if (response_thread_.joinable()) {
    try {
      response_thread_.join();
      LOG_DEBUG(instrument_name_.c_str(), "PROXY",
                "Response thread joined successfully");
    } catch (...) {
      LOG_WARN(instrument_name_.c_str(), "PROXY",
               "Exception joining response thread, detaching");
      response_thread_.detach();
    }
  }

  cleanup_pending_promises();
  cleanup_ipc();
  LOG_INFO(instrument_name_.c_str(), "PROXY", "Worker proxy stopped");
}

void InstrumentWorkerProxy::send_shutdown_message() {
  if (ipc_queue_ && ipc_queue_->is_valid()) {
    ipc::IPCMessage shutdown_msg;
    shutdown_msg.type = ipc::IPCMessage::Type::SHUTDOWN;
    shutdown_msg.id = 0;
    shutdown_msg.sync_token = 0;
    shutdown_msg.payload_size = 0;
    ipc_queue_->send(shutdown_msg, std::chrono::milliseconds(100));
  }
}

void InstrumentWorkerProxy::stop_worker_process() {
  if (worker_pid_ == 0) {
    return;
  }
  if (!get_process_manager().wait_for_exit(
          worker_pid_, std::chrono::milliseconds(PROCESS_KILL_TIMEOUT_MS))) {
    LOG_WARN(instrument_name_.c_str(), "PROXY", "Force killing worker");
    get_process_manager().kill_process(worker_pid_, true);
  }
}

void InstrumentWorkerProxy::join_response_thread_with_timeout() {
  if (response_thread_.joinable()) {
    // Give thread 500ms to exit gracefully
    running_.store(false, std::memory_order_release);

    auto start = std::chrono::steady_clock::now();
    while (response_thread_.joinable()) {
      auto elapsed = std::chrono::steady_clock::now() - start;
      if (elapsed > std::chrono::milliseconds(500)) {
        LOG_WARN(instrument_name_.c_str(), "PROXY",
                 "Response thread did not exit in time, detaching");
        response_thread_.detach();
        break;
      }

      // Try to join with timeout
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      if (response_thread_.joinable()) {
        try {
          response_thread_.join();
          LOG_DEBUG(instrument_name_.c_str(), "PROXY",
                    "Response thread joined successfully");
          break;
        } catch (...) {
          // Thread still running, continue waiting
        }
      }
    }
  }
}

void InstrumentWorkerProxy::cleanup_pending_promises() {
  std::lock_guard<std::mutex> lock(pending_mutex_);
  for (auto &[msg_id, promise] : pending_responses_) {
    CommandResponse error_resp;
    error_resp.instrument_name = instrument_name_;
    error_resp.success = false;
    error_resp.error_message = "Worker stopped";
    try {
      promise.set_value(std::move(error_resp));
    } catch (...) {
    }
  }
  pending_responses_.clear();
}

void InstrumentWorkerProxy::cleanup_ipc() {
  ipc_queue_.reset();
  ipc::SharedQueue::cleanup(instrument_name_);
}

std::future<CommandResponse>
InstrumentWorkerProxy::execute(SerializedCommand cmd) {
  std::promise<CommandResponse> promise;
  auto future = promise.get_future();

  uint64_t msg_id = next_message_id_++;
  cmd.id = fmt::format("{}-{}", instrument_name_, msg_id);

  LOG_INFO(instrument_name_.c_str(), cmd.id.c_str(),
           "Enqueueing command:  %s (sync=%d)", cmd.verb.c_str(),
           cmd.sync_token.value_or(0));

  // Store promise for response
  {
    std::lock_guard lock(pending_mutex_);
    pending_responses_[msg_id] = std::move(promise);
  }

  // Serialize and send command
  std::string payload = ipc::serialize_command(cmd);

  ipc::IPCMessage msg;
  msg.type = ipc::IPCMessage::Type::COMMAND;
  msg.id = msg_id;
  msg.sync_token = cmd.sync_token.value_or(0);
  msg.payload_size = std::min(payload.size(), sizeof(msg.payload));
  std::memcpy(msg.payload.data(), payload.data(), msg.payload_size);

  LOG_INFO(instrument_name_.c_str(), cmd.id.c_str(),
           "execute: sending msg_id=%d verb='%s' to req queue", msg_id,
           cmd.verb.c_str());

  if (!ipc_queue_->send(msg, cmd.timeout)) {
    LOG_ERROR(
        instrument_name_.c_str(), cmd.id.c_str(),
        "Failed to send command msg_id=%d verb='%s' (req queue send timed out)",
        msg_id, cmd.verb.c_str());

    // Fulfill promise with error
    CommandResponse error_resp;
    error_resp.command_id = cmd.id;
    error_resp.instrument_name = instrument_name_;
    error_resp.success = false;
    error_resp.error_message = "IPC send timeout";

    std::lock_guard lock(pending_mutex_);
    auto it = pending_responses_.find(msg_id);
    if (it != pending_responses_.end()) {
      it->second.set_value(error_resp);
      pending_responses_.erase(it);
    }
  } else {
    std::lock_guard lock(stats_mutex_);
    stats_.commands_sent++;
  }

  return future;
}

CommandResponse
InstrumentWorkerProxy::execute_sync(SerializedCommand cmd,
                                    std::chrono::milliseconds timeout) {
  // Capture identifying info before the move consumes cmd
  const std::string verb = cmd.verb;
  const std::string instrument = cmd.instrument_name;

  auto future = execute(std::move(cmd));

  LOG_INFO(instrument_name_.c_str(), "PROXY",
           "execute_sync: waiting for response verb='%s' timeout=%dms",
           verb.c_str(), timeout.count());

  if (future.wait_for(timeout) == std::future_status::ready) {
    LOG_INFO(instrument_name_.c_str(), "PROXY",
             "execute_sync: response received for verb='%s'", verb.c_str());
    return future.get();
  }
  LOG_WARN(instrument_name_.c_str(), "PROXY",
           "execute_sync TIMED OUT after %dms waiting for verb='%s' on '%s'",
           timeout.count(), verb.c_str(), instrument.c_str());

  CommandResponse timeout_resp;
  timeout_resp.instrument_name = instrument_name_;
  timeout_resp.success = false;
  timeout_resp.error_message = "Command timeout";

  std::lock_guard lock(stats_mutex_);
  stats_.commands_timeout++;

  return timeout_resp;
}

bool InstrumentWorkerProxy::is_alive() const {
  if (worker_pid_ == 0) {
    return false;
  }
  return get_process_manager().is_alive(worker_pid_);
}

InstrumentWorkerProxy::Stats InstrumentWorkerProxy::get_stats() const {
  std::lock_guard lock(stats_mutex_);
  return stats_;
}

void InstrumentWorkerProxy::response_listener_loop() {
  LOG_INFO(instrument_name_.c_str(), "PROXY", "Response listener started");
  while (true) {
    if (!ipc_queue_ || !ipc_queue_->is_valid()) {
      LOG_WARN(instrument_name_.c_str(), "PROXY",
               "IPC queue invalid, exiting listener");
      break;
    }
    ipc::IPCMessage msg;

    if (!ipc_queue_->receive_blocking(msg)) {
      continue;
    }
    // Exit immediately if stopping
    if (!running_.load(std::memory_order_relaxed)) {
      break;
    }

    try {
      handle_ipc_message(msg);
    } catch (const std::exception &e) {
      LOG_ERROR(instrument_name_.c_str(), "PROXY",
                "Exception processing message ID %llu: %s",
                (unsigned long long)msg.id, e.what());
    } catch (...) {
      LOG_ERROR(instrument_name_.c_str(), "PROXY",
                "Unknown exception processing message ID %llu",
                (unsigned long long)msg.id);
    }
  }
  LOG_INFO(instrument_name_.c_str(), "PROXY", "Response listener stopped");
}

void InstrumentWorkerProxy::handle_ipc_message(const ipc::IPCMessage &msg) {
  switch (msg.type) {
  case ipc::IPCMessage::Type::HEARTBEAT:
    get_process_manager().update_heartbeat(worker_pid_);
    break;
  case ipc::IPCMessage::Type::RESPONSE:
    try {
      handle_response_message(msg);
    } catch (const std::exception &e) {
      LOG_ERROR(instrument_name_.c_str(), "PROXY",
                "Failed to process response message ID %d: %s", msg.id,
                e.what());

      // Fulfill pending promise with error so the caller thread doesn't hang
      std::lock_guard<std::mutex> lock(pending_mutex_);
      auto it = pending_responses_.find(msg.id);
      if (it != pending_responses_.end()) {
        CommandResponse err_resp;
        err_resp.command_id = fmt::format("{}-{}", instrument_name_, msg.id);
        err_resp.instrument_name = instrument_name_;
        err_resp.success = false;
        err_resp.error_message =
            std::string("Malformed response payload: ") + e.what();

        try {
          it->second.set_value(std::move(err_resp));
        } catch (...) {
        }
        pending_responses_.erase(it);

        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.commands_failed++;
      }
    }
    break;
  case ipc::IPCMessage::Type::SYNC_ACK:
    handle_sync_ack_message(msg);
    break;
  default:
    LOG_WARN(instrument_name_.c_str(), "PROXY", "Unexpected message type: %d",
             static_cast<uint32_t>(msg.type));
  }
}

void InstrumentWorkerProxy::handle_response_message(
    const ipc::IPCMessage &msg) {
  std::string payload(msg.payload.data(), msg.payload_size);
  CommandResponse resp = ipc::deserialize_response(payload);
  LOG_DEBUG(instrument_name_.c_str(), resp.command_id.c_str(),
            "Received response msg_id=%d success=%d", msg.id,
            resp.success ? 1 : 0);

  std::lock_guard<std::mutex> lock(pending_mutex_);
  LOG_DEBUG(instrument_name_.c_str(), "PROXY",
            "handle_response_message: looking up msg_id=%d in "
            "pending_responses_ (size=%d)",
            msg.id, pending_responses_.size());
  auto it = pending_responses_.find(msg.id);
  if (it != pending_responses_.end()) {
    try {
      it->second.set_value(std::move(resp));
    } catch (const std::future_error &) {
    }
    pending_responses_.erase(it);

    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    if (resp.success) {
      stats_.commands_completed++;
    } else {
      stats_.commands_failed++;
    }
  } else {
    LOG_WARN(instrument_name_.c_str(), "PROXY",
             "handle_response_message: no pending promise for msg_id=%d "
             "(pending_responses_ size=%d); response discarded",
             msg.id, pending_responses_.size());
  }
}

void InstrumentWorkerProxy::handle_sync_ack_message(
    const ipc::IPCMessage &msg) {
  uint64_t sync_token = msg.sync_token;

  LOG_DEBUG(instrument_name_.c_str(), "PROXY", "Received SYNC_ACK for token=%d",
            sync_token);

  // Notify sync coordinator
  bool barrier_complete =
      sync_coordinator_.handle_ack(sync_token, instrument_name_);

  if (barrier_complete) {
    LOG_INFO(instrument_name_.c_str(), "PROXY",
             "Sync barrier %d complete, broadcasting SYNC_CONTINUE",
             sync_token);

    // Send SYNC_CONTINUE to self
    send_sync_continue(sync_token);
  }
}

void InstrumentWorkerProxy::send_sync_continue(uint64_t sync_token) {
  if (!ipc_queue_ || !ipc_queue_->is_valid()) {
    LOG_WARN(instrument_name_.c_str(), "PROXY",
             "Cannot send SYNC_CONTINUE, queue invalid");
    return;
  }

  ipc::IPCMessage msg;
  msg.type = ipc::IPCMessage::Type::SYNC_CONTINUE;
  msg.id = 0;
  msg.sync_token = sync_token;
  msg.payload_size = 0;

  bool sent = ipc_queue_->send(msg, std::chrono::milliseconds(1000));

  if (sent) {
    LOG_DEBUG(instrument_name_.c_str(), "PROXY", "Sent SYNC_CONTINUE token=%d",
              sync_token);
  } else {
    LOG_ERROR(instrument_name_.c_str(), "PROXY",
              "Failed to send SYNC_CONTINUE token=%d", sync_token);
  }
}

void InstrumentWorkerProxy::send_buffer_ack(const std::string &buffer_id) {
  if (!ipc_queue_ || !ipc_queue_->is_valid()) {
    LOG_WARN(instrument_name_.c_str(), "PROXY",
             "Cannot send BUFFER_ACK, queue invalid");
    return;
  }

  ipc::IPCMessage msg;
  msg.type = ipc::IPCMessage::Type::BUFFER_ACK;
  msg.id = 0;
  msg.sync_token = 0;
  msg.payload_size = 0;
  std::strncpy(msg.data_buffer_id, buffer_id.c_str(),
               sizeof(msg.data_buffer_id) - 1);

  bool sent = ipc_queue_->send(msg, std::chrono::milliseconds(1000));

  if (sent) {
    LOG_DEBUG(instrument_name_.c_str(), "PROXY",
              "Sent BUFFER_ACK for buffer %s", buffer_id.c_str());
  } else {
    LOG_ERROR(instrument_name_.c_str(), "PROXY",
              "Failed to send BUFFER_ACK for buffer %s", buffer_id.c_str());
  }
}

void InstrumentWorkerProxy::handle_worker_death() {
  LOG_ERROR(instrument_name_.c_str(), "PROXY",
            "Worker process died unexpectedly");

  // Fail all pending commands
  std::lock_guard lock(pending_mutex_);
  for (auto &[msg_id, promise] : pending_responses_) {
    CommandResponse error_resp;
    error_resp.instrument_name = instrument_name_;
    error_resp.success = false;
    error_resp.error_message = "Worker process died";
    promise.set_value(error_resp);
  }
  pending_responses_.clear();
}

} // namespace instserver
