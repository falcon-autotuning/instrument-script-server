#include "instrument-server/server/InstrumentWorkerProxy.hpp"
#include "instrument-server/Logger.hpp"
#include "instrument-server/ipc/ProcessManager.hpp"
#include "instrument-server/ipc/WorkerProtocol.hpp"

namespace instserver {

// Global process manager instance
static ipc::ProcessManager g_process_manager;

InstrumentWorkerProxy::InstrumentWorkerProxy(const std::string &instrument_name,
                                             const std::string &plugin_path,
                                             const nlohmann::json &config,
                                             const nlohmann::json &api_def)
    : instrument_name_(instrument_name), plugin_path_(plugin_path),
      config_(config), api_def_(api_def), process_manager_(g_process_manager) {}

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
  worker_pid_ = process_manager_.spawn_worker(instrument_name_, plugin_path_);

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
  if (!running_)
    return;

  LOG_INFO(instrument_name_, "PROXY", "Stopping worker proxy");

  running_ = false;

  // Send shutdown message
  if (ipc_queue_ && ipc_queue_->is_valid()) {
    ipc::IPCMessage shutdown_msg;
    shutdown_msg.type = ipc::IPCMessage::Type::SHUTDOWN;
    shutdown_msg.id = 0;
    shutdown_msg.payload_size = 0;
    ipc_queue_->send(shutdown_msg, std::chrono::milliseconds(1000));
  }

  // Wait for worker to exit
  if (worker_pid_ != 0) {
    process_manager_.wait_for_exit(worker_pid_,
                                   std::chrono::milliseconds(5000));
    process_manager_.kill_process(worker_pid_, true);
  }

  // Join response thread
  if (response_thread_.joinable()) {
    response_thread_.join();
  }

  // Cleanup IPC queues
  ipc::SharedQueue::cleanup(instrument_name_);

  LOG_INFO(instrument_name_, "PROXY", "Worker proxy stopped");
}

std::future<CommandResponse>
InstrumentWorkerProxy::execute(SerializedCommand cmd) {
  std::promise<CommandResponse> promise;
  auto future = promise.get_future();

  uint64_t msg_id = next_message_id_++;
  cmd.id = fmt::format("{}-{}", instrument_name_, msg_id);

  LOG_DEBUG(instrument_name_, cmd.id, "Enqueueing command: {}", cmd.verb);

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
  return process_manager_.is_alive(worker_pid_);
}

InstrumentWorkerProxy::Stats InstrumentWorkerProxy::get_stats() const {
  std::lock_guard lock(stats_mutex_);
  return stats_;
}

void InstrumentWorkerProxy::response_listener_loop() {
  LOG_INFO(instrument_name_, "PROXY", "Response listener started");

  while (running_) {
    auto msg_opt = ipc_queue_->receive(std::chrono::milliseconds(1000));

    if (!msg_opt) {
      continue; // Timeout
    }

    auto &msg = *msg_opt;

    if (msg.type == ipc::IPCMessage::Type::HEARTBEAT) {
      process_manager_.update_heartbeat(worker_pid_);
      continue;
    }

    if (msg.type != ipc::IPCMessage::Type::RESPONSE) {
      LOG_WARN(instrument_name_, "PROXY", "Unexpected message type: {}",
               static_cast<uint32_t>(msg.type));
      continue;
    }

    // Deserialize response
    std::string payload(msg.payload, msg.payload_size);
    CommandResponse resp = ipc::deserialize_response(payload);

    LOG_DEBUG(instrument_name_, resp.command_id,
              "Received response: success={}", resp.success);

    // Find pending promise
    std::lock_guard lock(pending_mutex_);
    auto it = pending_responses_.find(msg.id);
    if (it != pending_responses_.end()) {
      it->second.set_value(resp);
      pending_responses_.erase(it);

      std::lock_guard stats_lock(stats_mutex_);
      if (resp.success) {
        stats_.commands_completed++;
      } else {
        stats_.commands_failed++;
      }
    } else {
      LOG_WARN(instrument_name_, resp.command_id,
               "Received response for unknown message ID:  {}", msg.id);
    }
  }

  LOG_INFO(instrument_name_, "PROXY", "Response listener stopped");
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
