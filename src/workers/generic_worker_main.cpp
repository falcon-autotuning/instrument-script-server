#include "instrument_script/Logger.hpp"
#include "instrument_script/ipc/SharedQueue.hpp"
#include "instrument_script/ipc/WorkerProtocol.hpp"
#include "instrument_script/plugin/PluginLoader.hpp"
#include <atomic>
#include <csignal>
#include <cstring>

using namespace instrument_script;

static std::atomic<bool> g_running{true};

void signal_handler(int signal) {
  LOG_INFO("WORKER", "SIGNAL", "Received signal: {}", signal);
  g_running = false;
}

// Convert SerializedCommand to PluginCommand
plugin::PluginCommand to_plugin_command(const SerializedCommand &cmd) {
  plugin::PluginCommand plugin_cmd = {};

  strncpy(plugin_cmd.id, cmd.id.c_str(), PLUGIN_MAX_STRING_LEN - 1);
  strncpy(plugin_cmd.instrument_name, cmd.instrument_name.c_str(),
          PLUGIN_MAX_STRING_LEN - 1);
  strncpy(plugin_cmd.verb, cmd.verb.c_str(), PLUGIN_MAX_STRING_LEN - 1);

  plugin_cmd.timeout_ms = static_cast<uint32_t>(cmd.timeout.count());
  plugin_cmd.expects_response = cmd.expects_response;

  // Convert params
  plugin_cmd.param_count = 0;
  for (const auto &[key, val] : cmd.params) {
    if (plugin_cmd.param_count >= PLUGIN_MAX_PARAMS)
      break;

    auto &param = plugin_cmd.params[plugin_cmd.param_count++];
    strncpy(param.name, key.c_str(), PLUGIN_MAX_STRING_LEN - 1);

    std::visit(
        [&](auto &&arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, int32_t>) {
            param.value.type = PARAM_TYPE_INT32;
            param.value.value.i32_val = arg;
          } else if constexpr (std::is_same_v<T, int64_t>) {
            param.value.type = PARAM_TYPE_INT64;
            param.value.value.i64_val = arg;
          } else if constexpr (std::is_same_v<T, uint32_t>) {
            param.value.type = PARAM_TYPE_UINT32;
            param.value.value.u32_val = arg;
          } else if constexpr (std::is_same_v<T, uint64_t>) {
            param.value.type = PARAM_TYPE_UINT64;
            param.value.value.u64_val = arg;
          } else if constexpr (std::is_same_v<T, float>) {
            param.value.type = PARAM_TYPE_FLOAT;
            param.value.value.f_val = arg;
          } else if constexpr (std::is_same_v<T, double>) {
            param.value.type = PARAM_TYPE_DOUBLE;
            param.value.value.d_val = arg;
          } else if constexpr (std::is_same_v<T, bool>) {
            param.value.type = PARAM_TYPE_BOOL;
            param.value.value.b_val = arg;
          } else if constexpr (std::is_same_v<T, std::string>) {
            param.value.type = PARAM_TYPE_STRING;
            strncpy(param.value.value.str_val, arg.c_str(),
                    PLUGIN_MAX_STRING_LEN - 1);
          } else if constexpr (std::is_same_v<T, std::vector<double>>) {
            param.value.type = PARAM_TYPE_ARRAY_DOUBLE;
            // Note: For large arrays, would need dynamic allocation
            // This is simplified for demonstration
          }
        },
        val);
  }

  return plugin_cmd;
}

// Convert PluginResponse to CommandResponse
CommandResponse
from_plugin_response(const plugin::PluginResponse &plugin_resp) {
  CommandResponse resp;
  resp.command_id = plugin_resp.command_id;
  resp.instrument_name = plugin_resp.instrument_name;
  resp.success = plugin_resp.success;
  resp.error_code = plugin_resp.error_code;
  resp.error_message = plugin_resp.error_message;
  resp.text_response = plugin_resp.text_response;

  // Convert return value
  switch (plugin_resp.return_value.type) {
  case PARAM_TYPE_INT32:
    resp.return_value =
        static_cast<int64_t>(plugin_resp.return_value.value.i32_val);
    break;
  case PARAM_TYPE_INT64:
    resp.return_value = plugin_resp.return_value.value.i64_val;
    break;
  case PARAM_TYPE_DOUBLE:
    resp.return_value = plugin_resp.return_value.value.d_val;
    break;
  case PARAM_TYPE_BOOL:
    resp.return_value = plugin_resp.return_value.value.b_val;
    break;
  case PARAM_TYPE_STRING:
    resp.return_value = std::string(plugin_resp.return_value.value.str_val);
    break;
  default:
    break;
  }

  resp.started = std::chrono::steady_clock::now();
  resp.finished = std::chrono::steady_clock::now();

  return resp;
}

int main(int argc, char **argv) {
  // Parse command line arguments
  std::string instrument_name;
  std::string plugin_path;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--instrument" && i + 1 < argc) {
      instrument_name = argv[++i];
    } else if (arg == "--plugin" && i + 1 < argc) {
      plugin_path = argv[++i];
    }
  }

  if (instrument_name.empty() || plugin_path.empty()) {
    fprintf(stderr, "Usage: %s --instrument <name> --plugin <path>\n", argv[0]);
    return 1;
  }

  // Initialize logger
  InstrumentLogger::instance().init("worker_" + instrument_name + ".log",
                                    spdlog::level::debug);

  LOG_INFO(instrument_name, "WORKER_MAIN", "Starting worker for instrument: {}",
           instrument_name);
  LOG_INFO(instrument_name, "WORKER_MAIN", "Plugin path: {}", plugin_path);

  // Install signal handlers
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  try {
    // Load plugin
    plugin::PluginLoader plugin(plugin_path);

    if (!plugin.is_loaded()) {
      LOG_ERROR(instrument_name, "WORKER_MAIN", "Failed to load plugin: {}",
                plugin.get_error());
      return 1;
    }

    auto metadata = plugin.get_metadata();
    LOG_INFO(instrument_name, "WORKER_MAIN", "Loaded plugin: {} v{}",
             metadata.name, metadata.version);

    // Open IPC queues
    auto ipc_queue = ipc::SharedQueue::create_worker_queue(instrument_name);

    if (!ipc_queue.is_valid()) {
      LOG_ERROR(instrument_name, "WORKER_MAIN", "Failed to open IPC queues");
      return 1;
    }

    LOG_INFO(instrument_name, "WORKER_MAIN", "IPC queues opened");

    // Initialize plugin (config will be sent via first message or loaded
    // separately) For now, we'll pass empty config
    plugin::PluginConfig config = {};
    strncpy(config.instrument_name, instrument_name.c_str(),
            PLUGIN_MAX_STRING_LEN - 1);

    int32_t init_result = plugin.initialize(config);
    if (init_result != 0) {
      LOG_ERROR(instrument_name, "WORKER_MAIN",
                "Plugin initialization failed: {}", init_result);
      return 1;
    }

    LOG_INFO(instrument_name, "WORKER_MAIN",
             "Plugin initialized, entering main loop");

    // Main message loop
    while (g_running) {
      auto msg_opt = ipc_queue.receive(std::chrono::milliseconds(1000));

      if (!msg_opt) {
        // Timeout, send heartbeat
        ipc::IPCMessage heartbeat;
        heartbeat.type = ipc::IPCMessage::Type::HEARTBEAT;
        heartbeat.id = 0;
        heartbeat.payload_size = 0;
        ipc_queue.send(heartbeat, std::chrono::milliseconds(100));
        continue;
      }

      auto &msg = *msg_opt;

      if (msg.type == ipc::IPCMessage::Type::SHUTDOWN) {
        LOG_INFO(instrument_name, "WORKER_MAIN", "Received shutdown message");
        break;
      }

      if (msg.type != ipc::IPCMessage::Type::COMMAND) {
        LOG_WARN(instrument_name, "WORKER_MAIN",
                 "Received unexpected message type: {}",
                 static_cast<uint32_t>(msg.type));
        continue;
      }

      // Deserialize command
      std::string payload(msg.payload, msg.payload_size);
      SerializedCommand cmd = ipc::deserialize_command(payload);

      LOG_DEBUG(instrument_name, cmd.id, "Received command: {}", cmd.verb);

      // Convert to plugin format
      auto plugin_cmd = to_plugin_command(cmd);
      plugin::PluginResponse plugin_resp = {};

      // Execute command
      int32_t exec_result = plugin.execute_command(plugin_cmd, plugin_resp);

      // Convert response
      CommandResponse resp = from_plugin_response(plugin_resp);

      if (exec_result != 0 && resp.success) {
        resp.success = false;
        resp.error_code = exec_result;
        resp.error_message = "Plugin execution returned error code:  " +
                             std::to_string(exec_result);
      }

      LOG_DEBUG(instrument_name, cmd.id, "Command executed: success={}",
                resp.success);

      // Serialize and send response
      std::string resp_payload = ipc::serialize_response(resp);

      ipc::IPCMessage resp_msg;
      resp_msg.type = ipc::IPCMessage::Type::RESPONSE;
      resp_msg.id = msg.id;
      resp_msg.payload_size =
          std::min(resp_payload.size(), sizeof(resp_msg.payload));
      std::memcpy(resp_msg.payload, resp_payload.data(), resp_msg.payload_size);

      if (!ipc_queue.send(resp_msg, std::chrono::milliseconds(1000))) {
        LOG_ERROR(instrument_name, cmd.id, "Failed to send response");
      }
    }

    LOG_INFO(instrument_name, "WORKER_MAIN", "Shutting down worker");
    plugin.shutdown();

  } catch (const std::exception &ex) {
    LOG_ERROR(instrument_name, "WORKER_MAIN", "Fatal exception: {}", ex.what());
    return 1;
  }

  return 0;
}
