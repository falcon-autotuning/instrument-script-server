#include "instrument-script-server/SerializedCommand.hpp"
#include "instrument-script-server/ipc/DataBufferManager.hpp"
#include "instrument-script-server/ipc/SharedQueue.hpp"
#include "instrument-script-server/plugin/PluginLoader.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <instrument-data.h>
#include <instrument-log/inst_logging.h>
#include <instrument-plugin.h>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
using namespace instserver;

std::atomic<bool> g_running{true};

// Stored at startup so async signal handlers can log the instrument name.
static char g_instrument_name_buf[256] = {};

void signal_handler(int sig) {
  (void)sig;
  g_running = false;
}

// Handler for fatal signals (SIGSEGV, SIGABRT, SIGFPE). Writes a brief
// async-signal-safe message to stderr (which the ISS daemon redirects to
// tests/hub/logiss-daemon.log), flushes the spdlog file sink, then re-raises so
// the OS can produce a core dump as normal.
void crash_signal_handler(int sig) {
  char buf[256];
  int n = snprintf(buf, sizeof(buf),
                   "[instrument-worker] CRASH signal %d in worker '%s'\n", sig,
                   g_instrument_name_buf);
  if (n > 0) {
#ifdef _WIN32
    _write(_fileno(stderr), buf, (size_t)n);
#else
    write(STDERR_FILENO, buf, (size_t)n);
#endif
  }
  signal(sig, SIG_DFL);
  raise(sig);
}

static PluginCommand to_plugin_command(const SerializedCommand &cmd) {
  PluginCommand pcmd = {};
  strncpy(pcmd.id, cmd.id.c_str(), PLUGIN_MAX_STRING_LEN - 1);
  strncpy(pcmd.instrument_name, cmd.instrument_name.c_str(),
          PLUGIN_MAX_STRING_LEN - 1);
  strncpy(pcmd.verb, cmd.verb.c_str(), PLUGIN_MAX_STRING_LEN - 1);
  pcmd.expects_response = cmd.expects_response;
  pcmd.param_count = 0;

  for (uint8_t i = 0; i < cmd.param_count; ++i) {
    const auto &src = cmd.params[i];

    if (pcmd.param_count >= PLUGIN_MAX_PARAMS) {
      break;
    }

    auto &param = pcmd.params[pcmd.param_count++];

    strncpy(param.name, src.name.c_str(), PLUGIN_MAX_STRING_LEN - 1);

    const auto &value = src.value;

    switch (value.type) {
    case ipc::IPCParamValue::Type::DOUBLE:
      param.value.type = PARAM_TYPE_DOUBLE;
      param.value.value.d_val = value.d;
      break;
    case ipc::IPCParamValue::Type::INT64:
      param.value.type = PARAM_TYPE_INT64;
      param.value.value.i64_val = value.i;
      break;
    case ipc::IPCParamValue::Type::BOOL:
      param.value.type = PARAM_TYPE_BOOL;
      param.value.value.b_val = value.b;
      break;
    case ipc::IPCParamValue::Type::STRING:
      param.value.type = PARAM_TYPE_STRING;
      strncpy(param.value.value.str_val, value.str.c_str(),
              PLUGIN_MAX_STRING_LEN - 1);
      break;
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
    ParamValue v;

    switch (presp.return_value.type) {
    case PARAM_TYPE_DOUBLE:
      v.type = ipc::IPCParamValue::Type::DOUBLE;
      v.d = presp.return_value.value.d_val;
      break;

    case PARAM_TYPE_INT64:
      v.type = ipc::IPCParamValue::Type::INT64;
      v.i = presp.return_value.value.i64_val;
      break;

    case PARAM_TYPE_STRING:
      v.type = ipc::IPCParamValue::Type::STRING;
      v.str = presp.return_value.value.str_val;
      break;

    case PARAM_TYPE_BOOL:
      v.type = ipc::IPCParamValue::Type::BOOL;
      v.b = presp.return_value.value.b_val;
      break;

    default:
      return resp; // unknown type → leave return_value unset
    }

    resp.return_value = std::move(v);
  }

  // Copy large data buffer fields
  resp.has_large_data = presp.has_large_data;
  if (presp.has_large_data) {
    resp.buffer_id = presp.data_buffer_id;
    resp.element_count = presp.data_element_count;
    //  Convert data type enum to string, matching instrument-data library
    //  ArrayType
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
    case 4:
      resp.data_type = "uint32";
      break;
    case 5:
      resp.data_type = "uint64";
      break;
    case 6:
      resp.data_type = "unit8";
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
constexpr auto HEARTBEAT_SEND_TIMEOUT = std::chrono::milliseconds(100);

class Instrument {
public:
  Instrument(const std::string &instrument_name, const std::string &plugin_path)
      : instrument_name_(instrument_name), plugin_path_(plugin_path),
        plugin_(plugin_path) {}

  int run() {
    if (!load_and_init_plugin()) {
      return 1;
    }
    if (!connect_ipc_queue()) {
      return 1;
    }
    start_heartbeat_thread();

    LOG_INFO(instrument_name_.c_str(), "WORKER_MAIN", "Entering main loop");
    main_loop();

    cleanup();
    LOG_INFO(instrument_name_.c_str(), "WORKER_MAIN", "Worker exited cleanly");
    return 0;
  }

private:
  std::string instrument_name_;
  std::string plugin_path_;
  std::thread heartbeat_thread_;
  plugin::PluginLoader plugin_;
  std::unique_ptr<ipc::SharedQueue> ipc_queue_;
  std::optional<uint64_t> waiting_sync_token_;
  std::chrono::steady_clock::time_point last_heartbeat_ =
      std::chrono::steady_clock::now();

  // NOLINTBEGIN(hicpp-avoid-c-arrays,
  // cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  template <size_t N> void copy_cstr(char (&dest)[N], const std::string &src) {
    size_t size = std::min(src.size(), N - 1);
    std::memcpy(dest, src.data(), size);
    dest[size] = '\0';
  }
  // NOLINTEND(hicpp-avoid-c-arrays,
  // cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  bool load_and_init_plugin() {
    if (!plugin_.is_loaded()) {
      LOG_ERROR(instrument_name_.c_str(), "WORKER_MAIN",
                "Failed to load plugin");
      return false;
    }
    log_plugin_metadata();

    PluginConfig config = {};
    copy_cstr(config.instrument_name, instrument_name_);
    copy_cstr(config.connection_json, "{}");

    int32_t init_result = plugin_.initialize(config);
    if (init_result != 0) {
      LOG_ERROR(instrument_name_.c_str(), "WORKER_MAIN",
                "Plugin initialization failed: %d", init_result);
      return false;
    }

    LOG_INFO(instrument_name_.c_str(), "WORKER_MAIN",
             "Plugin initialized successfully");
    return true;
  }

  void log_plugin_metadata() {
    auto metadata = plugin_.get_metadata();
    LOG_INFO(instrument_name_.c_str(), "WORKER_MAIN",
             "Loaded plugin:  %s v%s (%s)", metadata.name, metadata.version,
             metadata.protocol_type);
  }

  bool connect_ipc_queue() {
    try {
      ipc_queue_ = ipc::SharedQueue::create_worker_queue(instrument_name_);
    } catch (const std::exception &ex) {
      LOG_ERROR(instrument_name_.c_str(), "WORKER_MAIN",
                "Exception opening IPC queues: %s", ex.what());
      plugin_.shutdown();
      return false;
    } catch (...) {
      LOG_ERROR(instrument_name_.c_str(), "WORKER_MAIN",
                "Unknown exception opening IPC queues");
      plugin_.shutdown();
      return false;
    }
    if (!ipc_queue_ || !ipc_queue_->is_valid()) {
      LOG_ERROR(instrument_name_.c_str(), "WORKER_MAIN",
                "Failed to create IPC queue");
      plugin_.shutdown();
      return false;
    }
    LOG_INFO(instrument_name_.c_str(), "WORKER_MAIN", "IPC queue connected");
    return true;
  }

  void main_loop() {
    uint64_t iteration = 0;

    while (g_running) {
      instserver::ipc::IPCMessage msg;

      if (!ipc_queue_->receive_blocking(msg)) {
        continue;
      }

      LOG_DEBUG(instrument_name_.c_str(), "WORKER_MAIN",
                "Received message type=%u",
                static_cast<unsigned int>(msg.type));

      process_message(msg);
      ++iteration;
    }

    LOG_INFO(instrument_name_.c_str(), "WORKER_MAIN",
             "Shutting down after %llu iters", (unsigned long long)iteration);
  }

  void start_heartbeat_thread() {
    heartbeat_thread_ = std::thread([this]() {
      while (g_running) {
        ipc::IPCMessage heartbeat;
        heartbeat.type = ipc::IPCMessage::Type::HEARTBEAT;
        heartbeat.id = 0;
        ipc_queue_->send(heartbeat, HEARTBEAT_SEND_TIMEOUT);
        std::this_thread::sleep_for(HEARTBEAT_INTERVAL);
      }
    });
  }

  void process_message(ipc::IPCMessage &msg) {
    switch (msg.type) {
    case ipc::IPCMessage::Type::SHUTDOWN:
      handle_shutdown();
      break;
    case ipc::IPCMessage::Type::SYNC_CONTINUE:
      handle_sync_continue(msg);
      break;
    case ipc::IPCMessage::Type::BUFFER_ACK:
      handle_buffer_ack(msg);
      break;
    case ipc::IPCMessage::Type::COMMAND:
      if (!waiting_sync_token_) {
        handle_command(msg);
      } else {
        LOG_DEBUG(instrument_name_.c_str(), "WORKER_MAIN",
                  "Blocked on sync token=%llu, ignoring message",
                  (unsigned long long)*waiting_sync_token_);
      }
      break;
    default:
      LOG_WARN(instrument_name_.c_str(), "WORKER_MAIN",
               "Received unexpected message type: %u",
               static_cast<unsigned int>(msg.type));
      break;
    }
  }

  void handle_shutdown() {
    LOG_INFO(instrument_name_.c_str(), "WORKER_MAIN",
             "Received shutdown message");
    g_running = false;
  }

  void handle_sync_continue(const ipc::IPCMessage &msg) {
    if (waiting_sync_token_ && msg.sync_token == *waiting_sync_token_) {
      LOG_DEBUG(instrument_name_.c_str(), "WORKER_MAIN",
                "Received SYNC_CONTINUE for token=%llu, proceeding",
                (unsigned long long)msg.sync_token);
      waiting_sync_token_.reset();
    } else {
      LOG_WARN(instrument_name_.c_str(), "WORKER_MAIN",
               "Unexpected SYNC_CONTINUE token=%llu (waiting=%llu)",
               (unsigned long long)msg.sync_token,
               (unsigned long long)waiting_sync_token_.value_or(0));
    }
  }

  void handle_buffer_ack(const ipc::IPCMessage &msg) {
    try {
      const char *buffer_id = msg.buffer_ack.buffer_id;

      LOG_INFO(instrument_name_.c_str(), "WORKER_MAIN",
               "Received buffer ack for buffer %s", buffer_id);

      data_manager_release_buffer(buffer_id);

    } catch (const std::exception &e) {
      LOG_ERROR(instrument_name_.c_str(), "WORKER_MAIN",
                "Failed to release buffer %s. err: %s",
                msg.buffer_ack.buffer_id, e.what());
    }
  }

  void handle_command(const ipc::IPCMessage &msg) {
    SerializedCommand cmd;
    try {
      cmd = ipc::from_ipc_command(msg.command);
    } catch (const std::exception &e) {
      LOG_ERROR(instrument_name_.c_str(), "WORKER_MAIN",
                "Failed to deserialize command from message ID %llu: %s",
                (unsigned long long)msg.id, e.what());

      PluginResponse plugin_resp = {};
      plugin_resp.success = false;
      std::strncpy(plugin_resp.command_id, "unknown",
                   PLUGIN_MAX_STRING_LEN - 1);
      std::strncpy(plugin_resp.instrument_name, instrument_name_.c_str(),
                   PLUGIN_MAX_STRING_LEN - 1);
      std::strncpy(plugin_resp.error_message,
                   (std::string("Malformed command JSON: ") + e.what()).c_str(),
                   PLUGIN_MAX_STRING_LEN - 1);

      SerializedCommand stub_cmd;
      stub_cmd.id = "unknown";
      stub_cmd.instrument_name = instrument_name_;
      stub_cmd.verb = "unknown";

      send_command_response(msg, stub_cmd, plugin_resp);
      return;
    }

    LOG_INFO(instrument_name_.c_str(), "WORKER_MAIN",
             "Received command: %s (id=%s, sync=%llu)", cmd.verb.c_str(),
             cmd.id.c_str(), (unsigned long long)cmd.sync_token.value_or(0));

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
    } else if (cmd.verb == "__RELEASE_BUFFER__") {
      // Internal release command to free worker creator's ownership of the
      // buffer
      plugin_resp.success = true;
      std::strncpy(plugin_resp.command_id, cmd.id.c_str(),
                   PLUGIN_MAX_STRING_LEN - 1);
      std::strncpy(plugin_resp.instrument_name, cmd.instrument_name.c_str(),
                   PLUGIN_MAX_STRING_LEN - 1);

      std::string buffer_id;

      for (uint8_t i = 0; i < cmd.param_count; ++i) {
        const auto &p = cmd.params[i];

        if (p.name == "buffer_id") {
          if (p.value.type == ipc::IPCParamValue::Type::STRING) {
            buffer_id = p.value.str;
          }
          break; // found it, stop
        }
      }

      if (!buffer_id.empty()) {
        LOG_INFO(instrument_name_.c_str(), "WORKER_MAIN",
                 "Executing __RELEASE_BUFFER__ for buffer: %s",
                 buffer_id.c_str());
        ipc::DataBufferManager::instance().release_buffer(buffer_id);
        std::strncpy(plugin_resp.text_response, "RELEASED",
                     PLUGIN_MAX_PAYLOAD - 1);
      } else {
        plugin_resp.success = false;
        std::strncpy(plugin_resp.error_message, "Missing buffer_id param",
                     PLUGIN_MAX_STRING_LEN - 1);
      }
    } else {
      // Normal plugin execution
      exec_result =
          plugin_.execute_command(to_plugin_command(cmd), plugin_resp);
    }

    LOG_DEBUG(instrument_name_.c_str(), cmd.id.c_str(),
              "Command executed: result=%d success=%s", exec_result,
              plugin_resp.success ? "true" : "false");

    send_command_response(msg, cmd, plugin_resp);

    if (cmd.sync_token) {
      uint64_t token = *cmd.sync_token;

      send_sync_ack(msg, token);

      if (cmd.is_sync_barrier) {
        waiting_sync_token_ = cmd.sync_token;

        LOG_DEBUG(instrument_name_.c_str(), cmd.id.c_str(),
                  "Now waiting for SYNC_CONTINUE token=%llu",
                  (unsigned long long)token);

      } else {
        LOG_DEBUG(instrument_name_.c_str(), cmd.id.c_str(),
                  "Received sync command (token=%llu), not final; continuing",
                  (unsigned long long)token);
      }

    } else {
      LOG_WARN(instrument_name_.c_str(), cmd.id.c_str(),
               "Received sync command but token was missing");
    }
  }

  void send_command_response(const ipc::IPCMessage &msg,
                             const SerializedCommand &cmd,
                             const PluginResponse &plugin_resp) {
    CommandResponse resp = from_plugin_response(plugin_resp);

    ipc::IPCMessage resp_msg{};
    resp_msg.type = ipc::IPCMessage::Type::RESPONSE;
    resp_msg.id = msg.id;
    resp_msg.sync_token = cmd.sync_token.value_or(0);

    ipc::fill_ipc_response(resp_msg.response, resp);

    LOG_DEBUG(instrument_name_.c_str(), cmd.id.c_str(),
              "send_command_response: sending response msg_id=%llu verb='%s' "
              "success=%s",
              (unsigned long long)msg.id, cmd.verb.c_str(),
              plugin_resp.success ? "true" : "false");
    bool resp_sent = ipc_queue_->send(resp_msg, IPC_SEND_TIMEOUT);
    if (!resp_sent) {
      LOG_WARN(instrument_name_.c_str(), cmd.id.c_str(),
               "send_command_response: DROPPED response for msg_id=%llu "
               "verb='%s' (resp queue full or timed out)",
               (unsigned long long)msg.id, cmd.verb.c_str());
    } else {
      LOG_DEBUG(instrument_name_.c_str(), cmd.id.c_str(),
                "send_command_response: response msg_id=%llu sent successfully",
                (unsigned long long)msg.id);
    }
  }

  void send_sync_ack(const ipc::IPCMessage &msg, uint64_t sync_token) {
    LOG_DEBUG(instrument_name_.c_str(), std::to_string(msg.id).c_str(),
              "Sending SYNC_ACK for token=%llu",
              (unsigned long long)sync_token);

    ipc::IPCMessage ack_msg;
    ack_msg.type = ipc::IPCMessage::Type::SYNC_ACK;
    ack_msg.id = msg.id;
    ack_msg.sync_token = sync_token;

    ipc_queue_->send(ack_msg, IPC_SEND_TIMEOUT);
  }

  void cleanup() {
    g_running = false;

    if (heartbeat_thread_.joinable()) {
      heartbeat_thread_.join();
    }

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

  std::string log_file = "worker_" + instrument_name + ".log";
  inst_log_init(log_file.c_str(), INST_LOG_DEBUG, "instrument",
                10 * 1024 * 1024, // 10 MB
                3);               // rotate 3 files
  LOG_INFO(instrument_name.c_str(), "WORKER_MAIN", "Worker starting");
  LOG_INFO(instrument_name.c_str(), "WORKER_MAIN", "Plugin: %s",
           plugin_path.c_str());
  strncpy(g_instrument_name_buf, instrument_name.c_str(),
          sizeof(g_instrument_name_buf) - 1);
  g_instrument_name_buf[sizeof(g_instrument_name_buf) - 1] = '\0';

  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
  std::signal(SIGSEGV, crash_signal_handler);
  std::signal(SIGABRT, crash_signal_handler);
  std::signal(SIGFPE, crash_signal_handler);

  try {
    int rc = Instrument(instrument_name, plugin_path).run();

    LOG_INFO(instrument_name.c_str(), "WORKER_MAIN",
             "Worker exited with code %d", rc);

    inst_log_flush();
    return rc;

  } catch (const std::exception &e) {
    LOG_ERROR(instrument_name.c_str(), "WORKER_MAIN",
              "Fatal error (std::exception): %s", e.what());

    inst_log_flush();
    return 1;

  } catch (...) {
    LOG_ERROR(instrument_name.c_str(), "WORKER_MAIN",
              "Fatal error: unknown exception type escaped main()");

    inst_log_flush();
    return 1;
  }
}
