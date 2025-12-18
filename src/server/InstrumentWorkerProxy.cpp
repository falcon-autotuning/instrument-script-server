#include "instrument-server/server/InstrumentWorkerProxy.hpp"
#include "instrument-server/Logger.hpp"
#include "instrument-server/ipc/ProcessManager.hpp"
#include "instrument-server/ipc/SharedQueue.hpp"

namespace instserver {

// Global process manager instance
static ipc::ProcessManager &get_process_manager() {
  static ipc::ProcessManager manager;
  return manager;
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
  LOG_INFO(instrument_name_, "PROXY", "Starting worker proxy");

  // Create IPC queues
  try {
    ipc_queue_ = ipc::SharedQueue::create_server_queue(instrument_name_);
  } catch (const std::exception &ex) {
    LOG_ERROR(instrument_name_, "PROXY", "Failed to create IPC queues: {}",
              ex.what());
    return false;
  }

  // Spawn worker process
  worker_pid_ =
      get_process_manager().spawn_worker(instrument_name_, plugin_path_);

  if (worker_pid_ == 0) {
    LOG_ERROR(instrument_name_, "PROXY", "Failed to spawn worker process");
    return false;
  }

  LOG_INFO(instrument_name_, "PROXY", "Worker process spawned:  PID={}",
           worker_pid_);

  // Start response listener thread
  running_ = true;
  response_thread_ = std::thread([this]() { response_listener_loop(); });

  // Wait a bit for worker to initialize
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  if (!is_alive()) {
    LOG_ERROR(instrument_name_, "PROXY", "Worker died during startup");
    stop();
    return false;
  }

  LOG_INFO(instrument_name_, "PROXY", "Worker proxy started successfully");
  return true;
}

void InstrumentWorkerProxy::stop() {
  if (!running_.exchange(false))
    return;
  LOG_INFO(instrument_name_, "PROXY", "Stopping worker proxy");

  send_shutdown_message();
  stop_worker_process();
  join_response_thread_with_timeout();
  cleanup_pending_promises();
  cleanup_ipc();

  LOG_INFO(instrument_name_, "PROXY", "Worker proxy stopped");
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
  if (worker_pid_ != 0) {
    if (!get_process_manager().wait_for_exit(worker_pid_,
                                             std::chrono::milliseconds(1000))) {
      LOG_WARN(instrument_name_, "PROXY", "Force killing worker");
      get_process_manager().kill_process(worker_pid_, true);
    }
  }
}

void InstrumentWorkerProxy::join_response_thread_with_timeout() {
  if (response_thread_.joinable()) {
    auto start = std::chrono::steady_clock::now();
    while (response_thread_.joinable()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      if (std::chrono::steady_clock::now() - start > std::chrono::seconds(1)) {
        LOG_ERROR(instrument_name_, "PROXY",
                  "Response thread did not exit in time, detaching");
        response_thread_.detach();
        return;
      }
    }
    if (response_thread_.joinable()) {
      response_thread_.join();
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

  LOG_DEBUG(instrument_name_, cmd.id, "Enqueueing command:  {} (sync={})",
            cmd.verb, cmd.sync_token.value_or(0));

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
  std::memcpy(msg.payload, payload.data(), msg.payload_size);

  if (!ipc_queue_->send(msg, cmd.timeout)) {
    LOG_ERROR(instrument_name_, cmd.id, "Failed to send command");

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
  auto future = execute(std::move(cmd));

  if (future.wait_for(timeout) == std::future_status::ready) {
    return future.get();
  } else {
    CommandResponse timeout_resp;
    timeout_resp.command_id = cmd.id;
    timeout_resp.instrument_name = instrument_name_;
    timeout_resp.success = false;
    timeout_resp.error_message = "Command timeout";

    std::lock_guard lock(stats_mutex_);
    stats_.commands_timeout++;

    return timeout_resp;
  }
}

bool InstrumentWorkerProxy::is_alive() const {
  if (worker_pid_ == 0)
    return false;
  return get_process_manager().is_alive(worker_pid_);
}

InstrumentWorkerProxy::Stats InstrumentWorkerProxy::get_stats() const {
  std::lock_guard lock(stats_mutex_);
  return stats_;
}

void InstrumentWorkerProxy::response_listener_loop() {
  LOG_INFO(instrument_name_, "PROXY", "Response listener started");
  while (running_.load(std::memory_order_relaxed)) {
    if (!ipc_queue_ || !ipc_queue_->is_valid()) {
      LOG_WARN(instrument_name_, "PROXY",
               "IPC queue invalid, exiting listener");
      break;
    }
    auto msg_opt = ipc_queue_->receive(std::chrono::milliseconds(100));
    if (!msg_opt)
      continue;
    handle_ipc_message(*msg_opt);
  }
  LOG_INFO(instrument_name_, "PROXY", "Response listener stopped");
}

void InstrumentWorkerProxy::handle_ipc_message(const ipc::IPCMessage &msg) {
  switch (msg.type) {
  case ipc::IPCMessage::Type::HEARTBEAT:
    get_process_manager().update_heartbeat(worker_pid_);
    break;
  case ipc::IPCMessage::Type::RESPONSE:
    handle_response_message(msg);
    break;
  case ipc::IPCMessage::Type::SYNC_ACK:
    handle_sync_ack_message(msg);
    break;
  default:
    LOG_WARN(instrument_name_, "PROXY", "Unexpected message type: {}",
             static_cast<uint32_t>(msg.type));
  }
}

void InstrumentWorkerProxy::handle_response_message(
    const ipc::IPCMessage &msg) {
  std::string payload(msg.payload, msg.payload_size);
  CommandResponse resp = ipc::deserialize_response(payload);
  LOG_DEBUG(instrument_name_, resp.command_id, "Received response: success={}",
            resp.success);

  std::lock_guard<std::mutex> lock(pending_mutex_);
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
  }
}

void InstrumentWorkerProxy::handle_sync_ack_message(
    const ipc::IPCMessage &msg) {
  uint64_t sync_token = msg.sync_token;

  LOG_DEBUG(instrument_name_, "PROXY", "Received SYNC_ACK for token={}",
            sync_token);

  // Notify sync coordinator
  bool barrier_complete =
      sync_coordinator_.handle_ack(sync_token, instrument_name_);

  if (barrier_complete) {
    LOG_INFO(instrument_name_, "PROXY",
             "Sync barrier {} complete, broadcasting SYNC_CONTINUE",
             sync_token);

    // Send SYNC_CONTINUE to self
    send_sync_continue(sync_token);
  }
}

void InstrumentWorkerProxy::send_sync_continue(uint64_t sync_token) {
  if (!ipc_queue_ || !ipc_queue_->is_valid()) {
    LOG_WARN(instrument_name_, "PROXY",
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
    LOG_DEBUG(instrument_name_, "PROXY", "Sent SYNC_CONTINUE token={}",
              sync_token);
  } else {
    LOG_ERROR(instrument_name_, "PROXY",
              "Failed to send SYNC_CONTINUE token={}", sync_token);
  }
}

void InstrumentWorkerProxy::handle_worker_death() {
  LOG_ERROR(instrument_name_, "PROXY", "Worker process died unexpectedly");

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
