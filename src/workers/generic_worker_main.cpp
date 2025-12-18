#include "instrument-server/Logger.hpp"
#include "instrument-server/SerializedCommand.hpp"
#include "instrument-server/ipc/SharedQueue.hpp"
#include "instrument-server/plugin/PluginLoader.hpp"
#include <chrono>
#include <csignal>
#include <iostream>
#include <queue>
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

  return resp;
}

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
    // Load plugin
    plugin::PluginLoader plugin(plugin_path);

    if (!plugin.is_loaded()) {
      LOG_ERROR(instrument_name, "WORKER_MAIN", "Failed to load plugin");
      return 1;
    }

    auto metadata = plugin.get_metadata();
    LOG_INFO(instrument_name, "WORKER_MAIN", "Loaded plugin:  {} v{} ({})",
             metadata.name, metadata.version, metadata.protocol_type);

    // TODO: Get config and initialize plugin properly
    // For now, use empty config
    PluginConfig config = {};
    strncpy(config.instrument_name, instrument_name.c_str(),
            PLUGIN_MAX_STRING_LEN - 1);
    strncpy(config.connection_json, "{}", PLUGIN_MAX_STRING_LEN - 1);

    int32_t init_result = plugin.initialize(config);
    if (init_result != 0) {
      LOG_ERROR(instrument_name, "WORKER_MAIN",
                "Plugin initialization failed: {}", init_result);
      return 1;
    }

    LOG_INFO(instrument_name, "WORKER_MAIN", "Plugin initialized successfully");

    // Connect to IPC queue
    auto ipc_queue = ipc::SharedQueue::create_worker_queue(instrument_name);

    if (!ipc_queue || !ipc_queue->is_valid()) {
      LOG_ERROR(instrument_name, "WORKER_MAIN", "Failed to create IPC queue");
      plugin.shutdown();
      return 1;
    }

    LOG_INFO(instrument_name, "WORKER_MAIN", "IPC queue connected");

    // Command queue for this worker
    std::queue<SerializedCommand> command_queue;
    std::optional<uint64_t> waiting_sync_token;

    // Main event loop
    auto last_heartbeat = std::chrono::steady_clock::now();
    const auto heartbeat_interval = std::chrono::milliseconds(500);

    LOG_INFO(instrument_name, "WORKER_MAIN", "Entering main loop");

    while (g_running) {
      // Send periodic heartbeat
      auto now = std::chrono::steady_clock::now();
      if (now - last_heartbeat >= heartbeat_interval) {
        ipc::IPCMessage heartbeat;
        heartbeat.type = ipc::IPCMessage::Type::HEARTBEAT;
        heartbeat.id = 0;
        heartbeat.payload_size = 0;
        ipc_queue->send(heartbeat, std::chrono::milliseconds(100));
        last_heartbeat = now;
      }

      // Check for messages
      auto msg_opt = ipc_queue->receive(std::chrono::milliseconds(100));

      if (!msg_opt) {
        continue;
      }

      ipc::IPCMessage &msg = *msg_opt;

      // Handle shutdown
      if (msg.type == ipc::IPCMessage::Type::SHUTDOWN) {
        LOG_INFO(instrument_name, "WORKER_MAIN", "Received shutdown message");
        break;
      }

      // Handle sync continue
      if (msg.type == ipc::IPCMessage::Type::SYNC_CONTINUE) {
        if (waiting_sync_token && msg.sync_token == *waiting_sync_token) {
          LOG_DEBUG(instrument_name, "WORKER_MAIN",
                    "Received SYNC_CONTINUE for token={}, proceeding",
                    msg.sync_token);
          waiting_sync_token.reset();
        } else {
          LOG_WARN(instrument_name, "WORKER_MAIN",
                   "Unexpected SYNC_CONTINUE token={} (waiting={})",
                   msg.sync_token, waiting_sync_token.value_or(0));
        }
        continue;
      }

      // Only process commands if not waiting on sync
      if (waiting_sync_token) {
        LOG_DEBUG(instrument_name, "WORKER_MAIN",
                  "Blocked on sync token={}, ignoring message",
                  *waiting_sync_token);
        continue;
      }

      // Handle command
      if (msg.type != ipc::IPCMessage::Type::COMMAND) {
        LOG_WARN(instrument_name, "WORKER_MAIN",
                 "Received unexpected message type:  {}",
                 static_cast<uint32_t>(msg.type));
        continue;
      }

      // Deserialize command
      std::string payload(msg.payload, msg.payload_size);
      SerializedCommand cmd = ipc::deserialize_command(payload);

      LOG_DEBUG(instrument_name, cmd.id, "Received command: {} (sync={})",
                cmd.verb, cmd.sync_token.value_or(0));

      // Convert to plugin format
      auto plugin_cmd = to_plugin_command(cmd);
      PluginResponse plugin_resp = {};

      // Execute command
      int32_t exec_result = plugin.execute_command(plugin_cmd, plugin_resp);

      LOG_DEBUG(instrument_name, cmd.id,
                "Command executed:  result={} success={}", exec_result,
                plugin_resp.success);

      // Convert response
      CommandResponse resp = from_plugin_response(plugin_resp);

      // Send response back
      std::string resp_payload = ipc::serialize_response(resp);

      ipc::IPCMessage resp_msg;
      resp_msg.type = ipc::IPCMessage::Type::RESPONSE;
      resp_msg.id = msg.id;
      resp_msg.sync_token = cmd.sync_token.value_or(0);
      resp_msg.payload_size =
          std::min(resp_payload.size(), sizeof(resp_msg.payload));
      std::memcpy(resp_msg.payload, resp_payload.data(), resp_msg.payload_size);

      ipc_queue->send(resp_msg, std::chrono::milliseconds(1000));

      // If this was a sync command, send ACK and block
      if (cmd.sync_token) {
        LOG_DEBUG(instrument_name, cmd.id, "Sending SYNC_ACK for token={}",
                  *cmd.sync_token);

        ipc::IPCMessage ack_msg;
        ack_msg.type = ipc::IPCMessage::Type::SYNC_ACK;
        ack_msg.id = msg.id;
        ack_msg.sync_token = *cmd.sync_token;
        ack_msg.payload_size = 0;

        ipc_queue->send(ack_msg, std::chrono::milliseconds(1000));

        // Block until SYNC_CONTINUE
        waiting_sync_token = cmd.sync_token;
        LOG_DEBUG(instrument_name, cmd.id,
                  "Now waiting for SYNC_CONTINUE token={}",
                  *waiting_sync_token);
      }
    }

    LOG_INFO(instrument_name, "WORKER_MAIN", "Shutting down");

    // Cleanup
    plugin.shutdown();
    ipc_queue.reset();

    LOG_INFO(instrument_name, "WORKER_MAIN", "Worker exited cleanly");
    return 0;

  } catch (const std::exception &e) {
    LOG_ERROR(instrument_name, "WORKER_MAIN", "Fatal error: {}", e.what());
    return 1;
  }
}
