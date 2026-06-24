#include "instrument-script-server/server/InstrumentWorkerProxy.hpp"
#include "instrument-script-server/ipc/ProcessManager.hpp"
#include "instrument-script-server/ipc/SharedQueue.hpp"
#include "instrument-script-server/server/InstrumentCommand.hpp"
#include "instrument-script-server/server/ServerDaemon.hpp"
#include "instserver/server/v1/daemon_messages.pb.h"
#include <fmt/format.h>
#include <instrument-log/inst_logging.h>

namespace instserver {
static std::string safe_string(const char *src, size_t max_len) {
  return {src, strnlen(src, max_len)};
}

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

InstrumentWorkerProxy::InstrumentWorkerProxy(std::string instrument_name,
                                             std::filesystem::path plugin,
                                             std::filesystem::path config,
                                             std::string log_level)
    : instrument_name_(std::move(instrument_name)),
      log_level_(std::move(log_level)), plugin_(std::move(plugin)),
      instrument_config_(std::move(config)) {}

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
  worker_pid_ = get_process_manager().spawn_worker(instrument_config_, plugin_,
                                                   log_level_);

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
  InstrumentConfig config;
  try {
    config = load_config(instrument_config_);
  } catch (const std::exception &e) {
    LOG_ERROR(instrument_name_.c_str(), "PROXY",
              "Failed to load config '%s': %s", instrument_config_.c_str(),
              e.what());
    return false;
  }
  const std::filesystem::path api_path =
      instrument_config_.parent_path() / config.api_ref;
  try {
    commands_ = load_api(api_path);
  } catch (const std::exception &e) {
    LOG_ERROR(instrument_name_.c_str(), "PROXY", "Invalid api '%s': %s\n",
              api_path.c_str(), e.what());
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
    ipc::IPCMessage wake_msg{};
    wake_msg.type = ipc::IPCMessage::Type::HEARTBEAT;
    ipc_queue_->send_to_response_queue(wake_msg, std::chrono::milliseconds(50));
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

  if (worker_pid_ != 0) {
    if (!get_process_manager().wait_for_exit(worker_pid_,
                                             std::chrono::seconds(2))) {
      LOG_WARN(instrument_name_.c_str(), "PROXY",
               "Worker did not exit gracefully, forcing kill");
      get_process_manager().kill_process(worker_pid_, true);
    }
  }

  cleanup_pending_promises();
  cleanup_ipc();
  commands_.clear();

  LOG_INFO(instrument_name_.c_str(), "PROXY", "Worker proxy stopped");
}
const Command *
InstrumentWorkerProxy::find_command(const std::string &verb) const {

  auto cmd_it = commands_.find(verb);
  if (cmd_it == commands_.end()) {
    LOG_WARN(instrument_name_.c_str(), "PROXY",
             "Looking for command %s that was not found", verb.c_str());
    return nullptr;
  }

  return &cmd_it->second;
}

bool InstrumentWorkerProxy::command_expects_response(
    const std::string &verb) const {
  const Command *cmd = find_command(verb);
  if (cmd == nullptr) {
    return false;
  }
  return !cmd->returns.empty();
}

std::vector<IO>
InstrumentWorkerProxy::get_responses(const std::string &verb) const {

  const Command *cmd = find_command(verb);
  if (cmd == nullptr) {
    return {};
  }

  return cmd->returns;
}

std::vector<IO>
InstrumentWorkerProxy::get_parameters(const std::string &verb) const {

  const Command *cmd = find_command(verb);
  if (cmd == nullptr) {
    return {};
  }

  return cmd->parameters;
}

void InstrumentWorkerProxy::send_shutdown_message() {
  if (ipc_queue_ && ipc_queue_->is_valid()) {
    ipc::IPCMessage shutdown_msg;
    shutdown_msg.type = ipc::IPCMessage::Type::SHUTDOWN;
    shutdown_msg.sync_token = 0;
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

void InstrumentWorkerProxy::cleanup_pending_promises() {
  std::lock_guard<std::mutex> lock(pending_mutex_);
  for (auto &[msg_id, promise] : pending_responses_) {
    InstrumentCommandResponse error_resp;
    error_resp.error_code = ErrorCode::WORKER_CRASHED;
    LOG_ERROR(instrument_name_.c_str(), "PROXY", "Worker stopped\n");
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

std::future<InstrumentCommandResponse>
InstrumentWorkerProxy::execute(InstrumentCommand cmd) {
  std::promise<InstrumentCommandResponse> promise;
  auto future = promise.get_future();

  uint64_t msg_id = next_message_id_++;
  cmd.id = fmt::format("{}-{}", instrument_name_, msg_id);

  LOG_INFO(instrument_name_.c_str(), cmd.id.c_str(),
           "Enqueueing command:  %s (sync=%d)", cmd.verb.c_str(),
           cmd.sync_token.value_or(0));

  // Store promise for response
  {
    std::lock_guard lock(pending_mutex_);
    pending_responses_[cmd.id] = std::move(promise);
  }

  // Serialize and send command
  std::vector<ipc::IPCMessage> msg;
  ipc::fill_ipc_commands(msg, cmd);

  LOG_INFO(instrument_name_.c_str(), cmd.id.c_str(),
           "execute: sending msg_id=%d verb='%s' to req queue", msg_id,
           cmd.verb.c_str());
  bool send_ok = true;

  for (const auto &m : msg) {
    if (!ipc_queue_->send(m, cmd.timeout)) {
      send_ok = false;
      break;
    }
  }
  if (!send_ok) {
    LOG_ERROR(instrument_name_.c_str(), cmd.id.c_str(),
              "Failed to send command msg_id=%d verb='%s' "
              "(req queue send timed out)",
              msg_id, cmd.verb.c_str());

    // Fulfill promise with error
    InstrumentCommandResponse error_resp;
    error_resp.id = cmd.id;
    error_resp.error_code = ErrorCode::IPC_SEND_TIMEOUT;
    LOG_ERROR(instrument_name_.c_str(), "PROXY", "IPC send timeout\n");

    std::lock_guard lock(pending_mutex_);
    auto it = pending_responses_.find(cmd.id);
    if (it != pending_responses_.end()) {
      it->second.set_value(error_resp);
      pending_responses_.erase(it);
    }
  } else {
    std::lock_guard lock(stats_mutex_);
    stats_.set_commands_sent(stats_.commands_sent() + 1);
  }

  return future;
}

InstrumentCommandResponse
InstrumentWorkerProxy::execute_sync(InstrumentCommand cmd,
                                    std::chrono::milliseconds timeout) {
  // Capture identifying info before the move consumes cmd
  const std::string verb = cmd.verb;

  auto future = execute(std::move(cmd));

  LOG_INFO(instrument_name_.c_str(), "PROXY",
           "execute_sync: waiting for response verb='%s' "
           "timeout=%dms",
           verb.c_str(), timeout.count());

  if (future.wait_for(timeout) == std::future_status::ready) {
    LOG_INFO(instrument_name_.c_str(), "PROXY",
             "execute_sync: response received for verb='%s'", verb.c_str());
    return future.get();
  }
  LOG_WARN(instrument_name_.c_str(), "PROXY",
           "execute_sync TIMED OUT after %dms waiting for "
           "verb='%s'",
           timeout.count(), verb.c_str());

  InstrumentCommandResponse timeout_resp;
  timeout_resp.error_code = ErrorCode::SYNC_TIMEOUT;
  LOG_ERROR(instrument_name_.c_str(), "PROXY", "Sync timeout\n");

  std::lock_guard lock(stats_mutex_);
  stats_.set_commands_timeout(stats_.commands_timeout() + 1);

  return timeout_resp;
}

bool InstrumentWorkerProxy::is_alive() const {
  if (worker_pid_ == 0) {
    return false;
  }
  return get_process_manager().is_alive(worker_pid_);
}

server::v1::InstrumentStats InstrumentWorkerProxy::get_stats() const {
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
                "Exception processing message ID %s: %s", msg.id.data(),
                e.what());
    } catch (std::exception &e) {
      LOG_ERROR(instrument_name_.c_str(), "PROXY",
                "Unknown exception processing message ID %s", e.what());
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
                "Failed to process response message ID %s: %s", msg.id.data(),
                e.what());

      // Fulfill pending promise with error so the caller
      // thread doesn't hang
      std::lock_guard<std::mutex> lock(pending_mutex_);
      std::string id = safe_string(msg.id.data(), msg.id.size());

      auto it = pending_responses_.find(id);
      if (it != pending_responses_.end()) {
        InstrumentCommandResponse err_resp;
        err_resp.id = fmt::format(
            "{}-{}", instrument_name_,
            std::string(msg.id.data(), strnlen(msg.id.data(), msg.id.size())));
        err_resp.error_code = ErrorCode::MALFORMED_IPC_FROM_WORKER;
        LOG_ERROR(instrument_name_.c_str(), "PROXY",
                  "Malformed response payload: %s\n", e.what());

        try {
          it->second.set_value(std::move(err_resp));
        } catch (...) {
        }
        pending_responses_.erase(it);

        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.set_commands_failed(stats_.commands_failed() + 1);
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

  std::string id = safe_string(msg.id.data(), msg.id.size());

  LOG_DEBUG(instrument_name_.c_str(), id.c_str(),
            "Received response chunk (return_count=%d / "
            "total=%d)",
            msg.response.return_count, msg.response.return_total);

  std::lock_guard<std::mutex> lock(pending_mutex_);

  auto &chunks = partial_responses_[id];
  chunks.push_back(msg);

  size_t accumulated = 0;
  for (const auto &m : chunks) {
    accumulated += m.response.return_count;
  }

  size_t expected = msg.response.return_total;

  if (accumulated < expected) {
    return;
  }

  InstrumentCommandResponse resp = ipc::from_ipc_responses(chunks);

  partial_responses_.erase(id);

  LOG_DEBUG(instrument_name_.c_str(), id.c_str(),
            "Reassembled response (%zu values)", accumulated);

  auto it = pending_responses_.find(id);
  if (it != pending_responses_.end()) {
    try {
      it->second.set_value(std::move(resp));
    } catch (const std::future_error &) {
    }

    pending_responses_.erase(it);

    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    if (resp.error_code == ErrorCode::NONE) {
      stats_.set_commands_completed(stats_.commands_completed() + 1);
    } else {
      stats_.set_commands_failed(stats_.commands_failed() + 1);
    }

  } else {
    LOG_WARN(instrument_name_.c_str(), "PROXY",
             "No pending promise for id=%s (discarding "
             "response)",
             id.c_str());
  }
}

void InstrumentWorkerProxy::handle_sync_ack_message(
    const ipc::IPCMessage &msg) {
  uint64_t sync_token = msg.sync_token;

  LOG_DEBUG(instrument_name_.c_str(), "PROXY",
            "Received SYNC_ACK for token=%llu", (unsigned long long)sync_token);

  // Notify sync coordinator
  bool barrier_complete =
      ServerDaemon::instance().sync_coordinator().handle_ack(sync_token,
                                                             instrument_name_);

  if (barrier_complete) {
    LOG_INFO(instrument_name_.c_str(), "PROXY",
             "Sync barrier %d complete, broadcasting "
             "SYNC_CONTINUE",
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

  ipc::IPCMessage msg{};
  msg.type = ipc::IPCMessage::Type::SYNC_CONTINUE;
  msg.sync_token = sync_token;

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

  ipc::IPCMessage msg{};
  msg.type = ipc::IPCMessage::Type::BUFFER_ACK;
  new (&msg.buffer_ack) ipc::IPCBufferAck{};
  msg.sync_token = 0;

  std::strncpy(msg.id.data(), buffer_id.c_str(), msg.id.size() - 1);
  msg.id[msg.id.size() - 1] = '\0';

  std::strncpy(msg.buffer_ack.buffer_id.data(), buffer_id.c_str(),
               msg.buffer_ack.buffer_id.size() - 1);
  msg.buffer_ack.buffer_id[msg.buffer_ack.buffer_id.size() - 1] = '\0';

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
    InstrumentCommandResponse error_resp;
    error_resp.error_code = ErrorCode::WORKER_CRASHED;
    LOG_ERROR(instrument_name_.c_str(), "PROXY", "Worker process died");
    promise.set_value(error_resp);
  }
  pending_responses_.clear();
}

} // namespace instserver
