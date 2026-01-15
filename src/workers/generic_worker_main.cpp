#include "instrument-server/Logger.hpp"
#include "instrument-server/SerializedCommand.hpp"
#include "instrument-server/ipc/SharedQueue.hpp"
#include "instrument-server/plugin/PluginLoader.hpp"
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>

using namespace instserver;

static volatile bool g_running = true;

void signal_handler(int sig) {
  (void)sig;
  g_running = false;
}

static PluginCommand to_plugin_command(const SerializedCommand &cmd) {
  PluginCommand pcmd = {};
  strncpy(pcmd.id, cmd.id.c_str(), PLUGIN_MAX_STRING_LEN - 1);
  strncpy(pcmd.instrument_name, cmd.instrument_name.c_str(),
          PLUGIN_MAX_STRING_LEN - 1);
  strncpy(pcmd.verb, cmd.verb.c_str(), PLUGIN_MAX_STRING_LEN - 1);
  pcmd.expects_response = cmd.expects_response;

  pcmd.param_count = 0;
  for (const auto &[key, value] : cmd.params) {
    if (pcmd.param_count >= PLUGIN_MAX_PARAMS)
      break;

    auto &param = pcmd.params[pcmd.param_count++];
    strncpy(param.name, key.c_str(), PLUGIN_MAX_STRING_LEN - 1);

    if (auto d = std::get_if<double>(&value)) {
      param.value.type = PARAM_TYPE_DOUBLE;
      param.value.value.d_val = *d;
    } else if (auto i = std::get_if<int64_t>(&value)) {
      param.value.type = PARAM_TYPE_INT64;
      param.value.value.i64_val = *i;
    } else if (auto s = std::get_if<std::string>(&value)) {
      param.value.type = PARAM_TYPE_STRING;
      strncpy(param.value.value.str_val, s->c_str(), PLUGIN_MAX_STRING_LEN - 1);
    } else if (auto b = std::get_if<bool>(&value)) {
      param.value.type = PARAM_TYPE_BOOL;
      param.value.value.b_val = *b;
    }
  }

  return pcmd;
}

static CommandResponse from_plugin_response(const PluginResponse &presp) {
  CommandResponse resp;
  resp.command_id = presp.command_id;
  resp.instrument_name = presp.instrument_name;
  resp.success = presp.success;
  resp.error_code = presp.error_code;
  resp.error_message = presp.error_message;
  resp.text_response = presp.text_response;

  // Convert return value
  if (presp.success && presp.return_value.type != PARAM_TYPE_NONE) {
    switch (presp.return_value.type) {
    case PARAM_TYPE_DOUBLE:
      resp.return_value = presp.return_value.value.d_val;
      break;
    case PARAM_TYPE_INT64:
      resp.return_value = presp.return_value.value.i64_val;
      break;
    case PARAM_TYPE_STRING:
      resp.return_value = std::string(presp.return_value.value.str_val);
      break;
    case PARAM_TYPE_BOOL:
      resp.return_value = presp.return_value.value.b_val;
      break;
    default:
      break;
    }
  }

  // Copy large data buffer fields
  resp.has_large_data = presp.has_large_data;
  if (presp.has_large_data) {
    resp.buffer_id = presp.data_buffer_id;
    resp.element_count = presp.data_element_count;

    // Convert data type enum to string
    switch (presp.data_type) {
    case 0:
      resp.data_type = "float32";
      break;
    case 1:
      resp.data_type = "float64";
      break;
    case 2:
      resp.data_type = "int32";
      break;
    case 3:
      resp.data_type = "int64";
      break;
    default:
      resp.data_type = "unknown";
      break;
    }
  }

  return resp;
}
namespace {
constexpr auto HEARTBEAT_INTERVAL = std::chrono::milliseconds(500);
constexpr auto IPC_SEND_TIMEOUT = std::chrono::milliseconds(1000);
constexpr auto IPC_RECV_TIMEOUT = std::chrono::milliseconds(100);
constexpr auto HEARTBEAT_SEND_TIMEOUT = std::chrono::milliseconds(100);

class Instrument {
public:
  Instrument(const std::string &instrument_name, const std::string &plugin_path)
      : instrument_name_(instrument_name), plugin_path_(plugin_path),
        plugin_(plugin_path) {}

  int run() {
    if (!load_and_init_plugin())
      return 1;
    if (!connect_ipc_queue())
      return 1;

    LOG_INFO(instrument_name_, "WORKER_MAIN", "Entering main loop");
    main_loop();

    cleanup();
    LOG_INFO(instrument_name_, "WORKER_MAIN", "Worker exited cleanly");
    return 0;
  }

private:
  std::string instrument_name_;
  std::string plugin_path_;
  plugin::PluginLoader plugin_;
  std::unique_ptr<ipc::SharedQueue> ipc_queue_;
  std::optional<uint64_t> waiting_sync_token_;
  std::chrono::steady_clock::time_point last_heartbeat_ =
      std::chrono::steady_clock::now();

  bool load_and_init_plugin() {
    if (!plugin_.is_loaded()) {
      LOG_ERROR(instrument_name_, "WORKER_MAIN", "Failed to load plugin");
      return false;
    }
    log_plugin_metadata();

    PluginConfig config = {};
    strncpy(config.instrument_name, instrument_name_.c_str(),
            PLUGIN_MAX_STRING_LEN - 1);
    strncpy(config.connection_json, "{}", PLUGIN_MAX_STRING_LEN - 1);

    int32_t init_result = plugin_.initialize(config);
    if (init_result != 0) {
      LOG_ERROR(instrument_name_, "WORKER_MAIN",
                "Plugin initialization failed: {}", init_result);
      return false;
    }

    LOG_INFO(instrument_name_, "WORKER_MAIN",
             "Plugin initialized successfully");
    return true;
  }

  void log_plugin_metadata() {
    auto metadata = plugin_.get_metadata();
    LOG_INFO(instrument_name_, "WORKER_MAIN", "Loaded plugin:  {} v{} ({})",
             metadata.name, metadata.version, metadata.protocol_type);
  }

  bool connect_ipc_queue() {
    ipc_queue_ = ipc::SharedQueue::create_worker_queue(instrument_name_);
    if (!ipc_queue_ || !ipc_queue_->is_valid()) {
      LOG_ERROR(instrument_name_, "WORKER_MAIN", "Failed to create IPC queue");
      plugin_.shutdown();
      return false;
    }
    LOG_INFO(instrument_name_, "WORKER_MAIN", "IPC queue connected");
    return true;
  }

  void main_loop() {
    while (g_running) {
      send_heartbeat_if_needed();
      auto msg_opt = ipc_queue_->receive(IPC_RECV_TIMEOUT);
      if (!msg_opt)
        continue;
      process_message(*msg_opt);
    }
    LOG_INFO(instrument_name_, "WORKER_MAIN", "Shutting down");
  }

  void send_heartbeat_if_needed() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_heartbeat_ >= HEARTBEAT_INTERVAL) {
      ipc::IPCMessage heartbeat;
      heartbeat.type = ipc::IPCMessage::Type::HEARTBEAT;
      heartbeat.id = 0;
      heartbeat.payload_size = 0;
      ipc_queue_->send(heartbeat, HEARTBEAT_SEND_TIMEOUT);
      last_heartbeat_ = now;
    }
  }

  void process_message(ipc::IPCMessage &msg) {
    switch (msg.type) {
    case ipc::IPCMessage::Type::SHUTDOWN:
      handle_shutdown();
      break;
    case ipc::IPCMessage::Type::SYNC_CONTINUE:
      handle_sync_continue(msg);
      break;
    case ipc::IPCMessage::Type::COMMAND:
      if (!waiting_sync_token_) {
        handle_command(msg);
      } else {
        LOG_DEBUG(instrument_name_, "WORKER_MAIN",
                  "Blocked on sync token={}, ignoring message",
                  *waiting_sync_token_);
      }
      break;
    default:
      LOG_WARN(instrument_name_, "WORKER_MAIN",
               "Received unexpected message type:  {}",
               static_cast<uint32_t>(msg.type));
      break;
    }
  }

  void handle_shutdown() {
    LOG_INFO(instrument_name_, "WORKER_MAIN", "Received shutdown message");
    g_running = false;
  }

  void handle_sync_continue(const ipc::IPCMessage &msg) {
    if (waiting_sync_token_ && msg.sync_token == *waiting_sync_token_) {
      LOG_DEBUG(instrument_name_, "WORKER_MAIN",
                "Received SYNC_CONTINUE for token={}, proceeding",
                msg.sync_token);
      waiting_sync_token_.reset();
    } else {
      LOG_WARN(instrument_name_, "WORKER_MAIN",
               "Unexpected SYNC_CONTINUE token={} (waiting={})", msg.sync_token,
               waiting_sync_token_.value_or(0));
    }
  }

  void handle_command(const ipc::IPCMessage &msg) {
    SerializedCommand cmd = deserialize_command_from_msg(msg);
    LOG_DEBUG(instrument_name_, cmd.id, "Received command: {} (sync={})",
              cmd.verb, cmd.sync_token.value_or(0));

    PluginResponse plugin_resp = {};
    int32_t exec_result = 0;

    if (cmd.verb == "__BARRIER_NOP__") {
      // Synthetic no-op barrier command used to include unused workers in a
      // sync barrier. Do not call plugin; return success immediately.
      plugin_resp.success = true;
      std::strncpy(plugin_resp.command_id, cmd.id.c_str(),
                   PLUGIN_MAX_STRING_LEN - 1);
      std::strncpy(plugin_resp.instrument_name, cmd.instrument_name.c_str(),
                   PLUGIN_MAX_STRING_LEN - 1);
      // Optionally set text_response
      std::strncpy(plugin_resp.text_response, "BARRIER_NOP",
                   PLUGIN_MAX_PAYLOAD - 1);
    } else {
      // Normal plugin execution
      exec_result =
          plugin_.execute_command(to_plugin_command(cmd), plugin_resp);
    }

    LOG_DEBUG(instrument_name_, cmd.id,
              "Command executed:  result={} success={}", exec_result,
              plugin_resp.success);

    send_command_response(msg, cmd, plugin_resp);

    if (cmd.sync_token) {
      send_sync_ack(msg, *cmd.sync_token);
      // Only block the worker after the final command for this token
      // (is_sync_barrier).
      if (cmd.is_sync_barrier) {
        waiting_sync_token_ = cmd.sync_token;
        LOG_DEBUG(instrument_name_, cmd.id,
                  "Now waiting for SYNC_CONTINUE token={}",
                  *waiting_sync_token_);
      } else {
        LOG_DEBUG(instrument_name_, cmd.id,
                  "Received sync command (token={}), not final; continuing",
                  *cmd.sync_token);
      }
    }
  }

  SerializedCommand deserialize_command_from_msg(const ipc::IPCMessage &msg) {
    std::string payload(msg.payload, msg.payload_size);
    return ipc::deserialize_command(payload);
  }

  void send_command_response(const ipc::IPCMessage &msg,
                             const SerializedCommand &cmd,
                             const PluginResponse &plugin_resp) {
    CommandResponse resp = from_plugin_response(plugin_resp);
    std::string resp_payload = ipc::serialize_response(resp);

    ipc::IPCMessage resp_msg;
    resp_msg.type = ipc::IPCMessage::Type::RESPONSE;
    resp_msg.id = msg.id;
    resp_msg.sync_token = cmd.sync_token.value_or(0);
    resp_msg.payload_size =
        std::min(resp_payload.size(), sizeof(resp_msg.payload));
    std::memcpy(resp_msg.payload, resp_payload.data(), resp_msg.payload_size);

    ipc_queue_->send(resp_msg, IPC_SEND_TIMEOUT);
  }

  void send_sync_ack(const ipc::IPCMessage &msg, uint64_t sync_token) {
    LOG_DEBUG(instrument_name_, std::to_string(msg.id),
              "Sending SYNC_ACK for token={}", sync_token);

    ipc::IPCMessage ack_msg;
    ack_msg.type = ipc::IPCMessage::Type::SYNC_ACK;
    ack_msg.id = msg.id;
    ack_msg.sync_token = sync_token;
    ack_msg.payload_size = 0;

    ipc_queue_->send(ack_msg, IPC_SEND_TIMEOUT);
  }

  void cleanup() {
    plugin_.shutdown();
    ipc_queue_.reset();
  }
};
} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <instrument_name> <plugin_path>\n";
    return 1;
  }

  std::string instrument_name = argv[1];
  std::string plugin_path = argv[2];

  // Setup logging for this worker
  std::string log_file = "worker_" + instrument_name + ".log";
  InstrumentLogger::instance().init(log_file, spdlog::level::debug);
  LOG_INFO(instrument_name, "WORKER_MAIN", "Worker starting");
  LOG_INFO(instrument_name, "WORKER_MAIN", "Plugin:  {}", plugin_path);

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  try {
    return Instrument(instrument_name, plugin_path).run();
  } catch (const std::exception &e) {
    LOG_ERROR(instrument_name, "WORKER_MAIN", "Fatal error: {}", e.what());
    return 1;
  }
}
