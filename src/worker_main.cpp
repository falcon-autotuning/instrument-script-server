#include "instrument-script-server/core/ErrorCodes.hpp"
#include "instrument-script-server/core/InstrumentCommand.hpp"
#include "instrument-script-server/core/ParsingTools.hpp"
#include "instrument-script-server/core/PluginLoader.hpp"
#include "instrument-script-server/ipc/IPCMessage.hpp"
#include "instrument-script-server/ipc/SharedQueue.hpp"
#include <algorithm>
#include <csignal>
#include <cxxopts.hpp>
#include <filesystem>
#include <instrument-data.h>
#include <instrument-log/inst_logging.h>
#include <instrument-plugin.h>
#include <iostream>
#include <mutex>
#include <plugin-host.h>
#include <queue>
#include <thread>
#include <unordered_map>
#include <variant>
#include <yaml-cpp/yaml.h>

#ifndef INSTSERVER_VERSION
#define INSTSERVER_VERSION "v0.0.0"
#endif
#ifndef INSTSERVER_GIT_TAG
#define INSTSERVER_GIT_TAG ""
#endif
#ifndef INSTSERVER_GIT_COMMIT
#define INSTSERVER_GIT_COMMIT "unknown"
#endif

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
using namespace instserver;

std::atomic<bool> g_running{true};

// Stored at startup so async signal handlers can log the instrument name.
static char g_instrument_name_buf[256] = {};
static std::string safe_string(const char *src, size_t max_len) {
  if (src == nullptr) {
    return {};
  }

  // Stop at null *without* reading out-of-bounds
  size_t len = 0;
  while (len < max_len) {
    if (src[len] == '\0') {
      break;
    }
    ++len;
  }

  return {src, len};
}

namespace {
constexpr auto HEARTBEAT_INTERVAL = std::chrono::milliseconds(500);
constexpr auto IPC_SEND_TIMEOUT = std::chrono::milliseconds(1000);
constexpr auto HEARTBEAT_SEND_TIMEOUT = std::chrono::milliseconds(100);
constexpr std::string_view BARRIER_NOP = "__BARRIER_NOP__";
constexpr std::string_view RELEASE_BUFFER = "__RELEASE_BUFFER__";
void signal_handler(int sig) {
  (void)sig;
  g_running = false;
}
void copy_string(char *dst, size_t dst_size, const char *src) {
  if (src == nullptr) {
    if (dst_size > 0)
      dst[0] = '\0';
    return;
  }
  std::strncpy(dst, src, dst_size - 1);
  dst[dst_size - 1] = '\0';
}

// Handler for fatal signals (SIGSEGV, SIGABRT, SIGFPE). Writes a brief
// async-signal-safe message to stderr (which the ISS daemon redirects to
// tests/hub/logiss-daemon.log), flushes the log file sink, then re-raises so
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

static std::unique_ptr<PluginCommand>
to_plugin_command(const InstrumentCommand &cmd) {
  auto pcmd = std::make_unique<PluginCommand>();
  strncpy(pcmd->id, cmd.id.c_str(), PLUGIN_MAX_STRING_LEN - 1);
  pcmd->id[PLUGIN_MAX_STRING_LEN - 1] = '\0';

  strncpy(pcmd->command, cmd.verb.c_str(), PLUGIN_MAX_STRING_LEN - 1);
  pcmd->command[PLUGIN_MAX_STRING_LEN - 1] = '\0';
  ParamStorage *ps = param_storage_create_with_capacity(cmd.params.size());
  for (const auto &param : cmd.params) {
    param_storage_push(ps, &param);
  }
  pcmd->params = ps;
  return pcmd;
}

static InstrumentCommandResponse
from_plugin_response(const PluginResponse &presp, std::string id,
                     std::string instrument_name, ErrorCode error_code) {
  InstrumentCommandResponse resp{};
  resp.error_code = error_code;
  resp.id = std::move(id);

  uint8_t count = plugin_response_count(&presp);
  resp.returns.reserve(count);
  for (uint8_t i = 0; i < count; ++i) {
    const Variable *src = plugin_response_get(&presp, i);
    if (src == nullptr) {
      continue;
    }

    Variable dst{};

    copy_string(dst.name, sizeof(dst.name), src->name);
    dst.type = src->type;
    switch (src->type) {
    case PARAM_TYPE_DOUBLE:
      dst.value.d_val = src->value.d_val;
      break;
    case PARAM_TYPE_INT64:
      dst.value.i64_val = src->value.i64_val;
      break;
    case PARAM_TYPE_STRING:
    case PARAM_TYPE_BUFFER:
      copy_string(dst.value.str_val, sizeof(dst.value.str_val),
                  src->value.str_val);
      break;
    case PARAM_TYPE_BOOL:
      dst.value.b_val = src->value.b_val;
      break;
    default:
      continue;
    }

    resp.returns.push_back(dst);
  }

  return resp;
}
constexpr size_t chunk_count(size_t total) {
  return (total + instserver::ipc::PARAM_CHUNK - 1) /
         instserver::ipc::PARAM_CHUNK;
}
constexpr std::string_view no_error(const ErrorCode code) {
  if (code == ErrorCode::NONE) {
    return "true";
  }
  return "false";
}
class InstrumentWorker {
public:
  InstrumentWorker(InstrumentConfig config, const std::string &plugin_path,
                   std::unordered_map<std::string, Command> commands)
      : config_(std::move(config)), plugin_path_(plugin_path),
        commands_(std::move(commands)), plugin_(plugin_path) {}

  ~InstrumentWorker() { cleanup(); }

  int run() {
    if (!load_and_init_plugin()) {
      return 1;
    }
    if (!connect_ipc_queue()) {
      return 1;
    }
    start_heartbeat_thread();

    log_info("Entering main loop");
    main_loop();
    log_info("Worker exited cleanly");
    return 0;
  }
  template <typename... Args>
  void log_info(const char *fmt, Args &&...args) const {
    inst_logf(INST_LOG_INFO, config_.name.c_str(), "WORKER_MAIN", fmt,
              std::forward<Args>(args)...);
  }
  template <typename... Args>
  void log_debug(const char *fmt, Args &&...args) const {
    inst_logf(INST_LOG_DEBUG, config_.name.c_str(), "WORKER_MAIN", fmt,
              std::forward<Args>(args)...);
  }
  template <typename... Args>
  void log_trace(const char *fmt, Args &&...args) const {
    inst_logf(INST_LOG_TRACE, config_.name.c_str(), "WORKER_MAIN", fmt,
              std::forward<Args>(args)...);
  }
  template <typename... Args>
  void log_error(const char *fmt, Args &&...args) const {
    inst_logf(INST_LOG_ERROR, config_.name.c_str(), "WORKER_MAIN", fmt,
              std::forward<Args>(args)...);
  }
  template <typename... Args>
  void log_warn(const char *fmt, Args &&...args) const {
    inst_logf(INST_LOG_WARN, config_.name.c_str(), "WORKER_MAIN", fmt,
              std::forward<Args>(args)...);
  }

private:
  InstrumentConfig config_;

  std::unordered_map<std::string, Command> commands_;
  std::unordered_map<std::string, std::vector<ipc::IPCMessage>>
      partial_commands_;
  std::mutex partial_commands_mutex_;
  std::string plugin_path_;
  std::thread heartbeat_thread_;
  plugin::PluginLoader plugin_;
  std::unique_ptr<ipc::SharedQueue> ipc_queue_;
  std::optional<uint64_t> waiting_sync_token_;
  std::chrono::steady_clock::time_point last_heartbeat_ =
      std::chrono::steady_clock::now();

  // Both of these are related to incoming messages and organizing
  std::unordered_map<uint64_t, std::queue<ipc::IPCMessage>>
      incoming_read_messages_{};

  using Incoming = std::variant<ipc::IPCMessage, // SINGLETON
                                uint64_t         // QUEUE (sync token)
                                >;
  std::queue<Incoming>
      queue_of_incoming_messages_{}; // The next index to pop comings after the
                                     // waiting_sync_token_

  // NOLINTBEGIN(hicpp-avoid-c-arrays,
  // cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  template <size_t N> void copy_cstr(char (&dest)[N], const std::string &src) {
    if constexpr (N > 0) {
      size_t size = std::min(src.size(), N - 1);
      std::memcpy(dest, src.data(), size);
      dest[size] = '\0';
    }
  }
  // NOLINTEND(hicpp-avoid-c-arrays,
  // cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
  bool load_and_init_plugin() {
    if (!plugin_.is_loaded()) {
      log_error("Failed to load plugin\n");
      return false;
    }
    log_plugin_metadata();

    auto config = std::make_unique<PluginConfig>();
    copy_cstr(config->instrument_name, config_.name);
    copy_cstr(config->address, config_.address.value_or(""));
    config->baud_rate = config_.baudrate.value_or(0);
    copy_cstr(config->custom, config_.custom.value_or(""));

    ErrorCode init_result = plugin_.initialize(config.get());
    if (init_result != ErrorCode::NONE) {
      log_error("Plugin initialization failed: %d\n", init_result);
      return false;
    }

    LOG_INFO(config_.name.c_str(), "WORKER_MAIN",
             "Plugin initialized successfully");
    return true;
  }

  void log_plugin_metadata() {
    auto metadata = plugin_.get_metadata();
    log_info("Loaded plugin:  %s v%s (%s)", metadata.name, metadata.version,
             metadata.protocol_type);
  }

  bool connect_ipc_queue() {
    try {
      ipc_queue_ = ipc::SharedQueue::create_worker_queue(config_.name);
    } catch (const std::exception &e) {
      log_error("Exception opening IPC queues: %s\n", e.what());
      plugin_.shutdown();
      return false;
    } catch (...) {
      log_error("Unknown exception opening IPC queues\n");
      plugin_.shutdown();
      return false;
    }
    if (!ipc_queue_ || !ipc_queue_->is_valid()) {
      log_error("Failed to create IPC queue\n");
      plugin_.shutdown();
      return false;
    }
    LOG_INFO(config_.name.c_str(), "WORKER_MAIN", "IPC queue connected");
    return true;
  }
  bool receive_queued(ipc::IPCMessage &msg) {
    if (!waiting_sync_token_.has_value() &&
        queue_of_incoming_messages_.size() > 0) {
      Incoming inc = queue_of_incoming_messages_.front();
      queue_of_incoming_messages_.pop();

      if (std::holds_alternative<ipc::IPCMessage>(inc)) {
        msg = std::get<ipc::IPCMessage>(inc);
        return true;
      }

      waiting_sync_token_ = std::get<uint64_t>(inc);
    }
    if (waiting_sync_token_.has_value()) {
      auto it = incoming_read_messages_.find(waiting_sync_token_.value());
      if (it != incoming_read_messages_.end() && !it->second.empty()) {
        msg = it->second.front();
        it->second.pop();
        return true;
      }
    }
    instserver::ipc::IPCMessage internal_msg{};
    if (!ipc_queue_->receive_blocking(internal_msg)) {
      throw std::runtime_error("IPC queue disconnected");
    }
    // investigate new message
    if ((waiting_sync_token_.has_value() &&
         internal_msg.sync_token == waiting_sync_token_.value()) ||
        (!waiting_sync_token_.has_value() && internal_msg.sync_token == 0)) {
      if (internal_msg.sync_token !=
          0) { // we don't consider 0 to be a sync token
        waiting_sync_token_ = internal_msg.sync_token;
      }
      msg = internal_msg;
      return true;
    }
    if (internal_msg.sync_token == 0) {
      Incoming inc = internal_msg;
      queue_of_incoming_messages_.emplace(inc);
      return false;
    }
    auto it = incoming_read_messages_.find(internal_msg.sync_token);
    if (it == incoming_read_messages_.end()) {
      Incoming inc = internal_msg.sync_token;
      queue_of_incoming_messages_.emplace(inc);
      it = incoming_read_messages_
               .emplace(internal_msg.sync_token, std::queue<ipc::IPCMessage>())
               .first;
    }
    it->second.push(internal_msg);
    return false;
  }

  void main_loop() {
    uint64_t iteration = 0;

    while (g_running) {
      instserver::ipc::IPCMessage msg{};

      if (!receive_queued(msg)) {
        continue;
      }

      log_debug("Received message type=%u",
                static_cast<unsigned int>(msg.type));

      process_message(msg);
      ++iteration;
    }

    LOG_INFO(config_.name.c_str(), "WORKER_MAIN",
             "Shutting down after %llu iters", (unsigned long long)iteration);
  }

  void start_heartbeat_thread() {
    heartbeat_thread_ = std::thread([this]() {
      while (g_running) {
        ipc::IPCMessage heartbeat{};
        heartbeat.type = ipc::IPCMessage::Type::HEARTBEAT;
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
      handle_command_chunk(msg);
      break;
    default:
      log_warn("Received unexpected message type: %u",
               static_cast<unsigned int>(msg.type));
      break;
    }
  }

  void handle_shutdown() const {
    log_info("Received shutdown message");
    g_running = false;
  }

  void handle_sync_continue(const ipc::IPCMessage &msg) {
    if (waiting_sync_token_.has_value() &&
        msg.sync_token == waiting_sync_token_.value()) {
      log_debug("Received SYNC_CONTINUE for token=%llu, proceeding",
                (unsigned long long)msg.sync_token);
      // deleting all other messages with our sync_token
      incoming_read_messages_.erase(waiting_sync_token_.value());
      waiting_sync_token_.reset();
    } else {
      log_warn("Unexpected SYNC_CONTINUE token=%llu (waiting=%llu)",
               (unsigned long long)msg.sync_token,
               (unsigned long long)waiting_sync_token_.value_or(0));
    }
  }

  void handle_buffer_ack(const ipc::IPCMessage &msg) {
    try {
      const char *buffer_id = msg.buffer_ack.buffer_id.data();
      log_info("Received buffer ack for buffer %s", buffer_id);
      data_manager_release_buffer(buffer_id);
    } catch (const std::exception &e) {
      log_error("Failed to release buffer %s. err: %s\n",
                msg.buffer_ack.buffer_id.data(), e.what());
    }
  }
  void handle_command_chunk(const ipc::IPCMessage &msg) {
    std::string id = safe_string(msg.id.data(), PLUGIN_MAX_STRING_LEN);

    std::lock_guard<std::mutex> lock(partial_commands_mutex_);
    auto &chunks = partial_commands_[id];
    if (chunks.empty()) {
      chunks.reserve(chunk_count(msg.command.param_total));
    }
    chunks.push_back(msg);

    size_t accumulated = 0;
    for (const auto &m : chunks) {
      accumulated += m.command.param_count;
    }

    size_t expected = msg.command.param_total;

    if (accumulated < expected) {
      return; // still waiting
    }

    std::vector<ipc::IPCMessage> full_chunks = std::move(chunks);
    partial_commands_.erase(id);

    handle_full_command(full_chunks);
  }
  void handle_full_command(const std::vector<ipc::IPCMessage> &chunks) {
    if (chunks.empty()) {
      log_error("handle_full_command called with empty chunks");
      return;
    }

    const ipc::IPCMessage &first = chunks.front();

    InstrumentCommand cmd;

    try {
      cmd = ipc::from_ipc_commands(chunks);
    } catch (const std::exception &e) {
      log_error("Failed to deserialize command ID %s: %s", first.id.data(),
                e.what());

      const PluginResponse *plugin_resp =
          plugin_response_create_with_capacity(0);

      InstrumentCommand stub_cmd{};
      stub_cmd.id = safe_string(first.id.data(), PLUGIN_MAX_STRING_LEN);
      stub_cmd.verb = "unknown";

      send_command_response(first, stub_cmd, *plugin_resp,
                            ErrorCode::MISSING_MESSAGE_ID);
      return;
    }

    execute_command(cmd, first);
  }

  void execute_command(const InstrumentCommand &cmd,
                       const ipc::IPCMessage &msg) {

    log_debug("cmd.verb.size = %zu", cmd.verb.size());
    log_debug("cmd.verb.data ptr = %p", cmd.verb.data());
    log_info("Received command: %s (id=%s, sync=%llu)", cmd.verb.c_str(),
             cmd.id.c_str(), (unsigned long long)cmd.sync_token.value_or(0));
    // ---- special commands ----
    if (cmd.verb == BARRIER_NOP) {
      return;
    }

    log_debug("After BARRIER_NOP");
    if (cmd.verb == RELEASE_BUFFER) {
      std::string buffer_id;

      for (const auto &p : cmd.params) {
        if (p.type == PARAM_TYPE_BUFFER) {
          const auto *const chars = p.value.str_val;
          buffer_id = std::string(chars, strnlen(chars, PLUGIN_MAX_STRING_LEN));
          break;
        }
      }

      if (buffer_id.empty()) {
        log_error("Missing buffer_id param");
        return;
      }

      log_info("Executing __RELEASE_BUFFER__ for buffer: %s",
               buffer_id.c_str());
      // This is releasing the plugin buffer for the user automatically
      data_manager_release_buffer(buffer_id.c_str());
      return;
    }
    log_debug("Before command search");
    const auto it = commands_.find(cmd.verb);
    log_debug("After command search");
    if (it == commands_.end()) {
      log_error("Did not find a instruction matching %s", cmd.verb.c_str());
      return;
    }
    log_debug("Found a matching command for %s", cmd.verb.c_str());
    const auto &command = it->second;
    log_debug("Checking that command %s matches what is defined in the config",
              cmd.verb.c_str());
    // Check command parameters, ignoring implicitly injected "channel" if not
    // expected by the API
    size_t actual_size = 0;
    size_t channel_param_index = -1;
    for (size_t i = 0; i < cmd.params.size(); ++i) {
      if (std::string_view(cmd.params[i].name) == "channel") {
        // Only count it if the API actually expects a parameter named "channel"
        bool expected_in_api = false;
        for (const auto &p : command.parameters) {
          if (p.name == "channel") {
            expected_in_api = true;
            break;
          }
        }
        if (!expected_in_api) {
          channel_param_index = i;
          continue;
        }
      }
      actual_size++;
    }

    auto expected_size = command.parameters.size();
    if (actual_size != expected_size) {
      log_error("Config command %s parameter mismatch: "
                "expected size='%d', got='%d'",
                cmd.verb.c_str(), expected_size, actual_size);
      return;
    }
    log_debug("The size of the command matches the one in the config with %d "
              "entries",
              actual_size);
    size_t api_idx = 0;
    for (size_t i = 0; i < cmd.params.size(); i++) {
      if (i == channel_param_index) {
        continue;
      }
      const auto &expected_name = command.parameters[api_idx].name;
      const auto &actual_name = cmd.params[i].name;
      if (std::string_view(actual_name) != expected_name) {
        log_error("Config command %s parameter mismatch at index %d: "
                  "expected='%s', got='%s'",
                  cmd.verb.c_str(), i, expected_name.c_str(), actual_name);
        return;
      }
      auto expected_type = command.parameters[api_idx].type;
      auto actual_type = cmd.params[i].type;
      if (actual_type != expected_type) {
        log_error("Config command %s parameter mismatch at index %d: "
                  "expected='%d', got='%d'",
                  cmd.verb.c_str(), i, expected_type, actual_type);
        return;
      }
      api_idx++;
    }
    log_debug("All of the parameters types and names match the API");
    auto expected_returns_size = command.returns.size();
    PluginResponse *plugin_resp =
        plugin_response_create_with_capacity(expected_returns_size);
    std::unique_ptr<PluginCommand> pcmd = to_plugin_command(cmd);

    // ---- normal execution ----
    ErrorCode exec_result = plugin_.execute_command(pcmd.get(), plugin_resp);
    log_info("Command executed: result=%u success=%s",
             static_cast<unsigned>(exec_result), no_error(exec_result).data());

    send_command_response(msg, cmd, *plugin_resp, exec_result);
    param_storage_free(pcmd->params);
    plugin_response_free(plugin_resp);

    // ---- sync handling ----
    if (!cmd.sync_token.has_value()) {
      return;
    }
    uint64_t token = cmd.sync_token.value();

    send_sync_ack(msg, token);
  }
  void send_command_response(const ipc::IPCMessage &msg,
                             const InstrumentCommand &cmd,
                             const PluginResponse &plugin_resp,
                             ErrorCode error_code) {
    const InstrumentCommandResponse resp = from_plugin_response(
        plugin_resp, safe_string(msg.id.data(), PLUGIN_MAX_STRING_LEN),
        config_.name, error_code);
    std::vector<ipc::IPCMessage> resp_msgs;
    ipc::fill_ipc_responses(resp_msgs, resp);
    log_info("send_command_response: sending response msg_id=%s verb='%s' "
             "success=%s",
             safe_string(msg.id.data(), PLUGIN_MAX_STRING_LEN).c_str(),
             cmd.verb.c_str(), no_error(resp.error_code).data());

    bool send_ok = true;

    for (const auto &m : resp_msgs) {
      if (!ipc_queue_->send(m, IPC_SEND_TIMEOUT)) {
        send_ok = false;
        break;
      }
    }

    if (!send_ok) {
      log_warn("send_command_response: DROPPED response (partial send) "
               "msg_id=%s verb='%s'",
               cmd.id.c_str(), cmd.verb.c_str());
    } else {
      log_debug("send_command_response: response msg_id=%s sent successfully "
                "(%zu chunks)",
                cmd.id.c_str(), resp_msgs.size());
    }
  }

  void send_sync_ack(const ipc::IPCMessage &msg, uint64_t sync_token) {
    log_debug("Sending SYNC_ACK for token=%llu",
              (unsigned long long)sync_token);

    ipc::IPCMessage ack_msg{};
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
uint8_t parse_log_level(const std::string &s) {
  if (s == "trace") {
    return INST_LOG_TRACE;
  }
  if (s == "debug") {
    return INST_LOG_DEBUG;
  }
  if (s == "info") {
    return INST_LOG_INFO;
  }
  if (s == "warn") {
    return INST_LOG_WARN;
  }
  if (s == "error") {
    return INST_LOG_ERROR;
  }
  throw std::runtime_error("Invalid log level: " + s);
}
void print_usage(const cxxopts::Options &options) {
  std::cout << options.help() << "\n";
}
} // namespace

int main(int argc, char **argv) {
  try {
    cxxopts::Options options("instrument-worker", "Instrument Worker Process");

    options.add_options()("h,help", "Show help")("v,version", "Show version")(
        "log-level", "Log level (trace|debug|info|warn|error)",
        cxxopts::value<std::string>()->default_value("info"))(
        "instrument_config", "Instrument config path",
        cxxopts::value<std::string>())("plugin", "Plugin path",
                                       cxxopts::value<std::string>());

    options.parse_positional({"instrument_config", "plugin"});

    auto result = options.parse(argc, argv);

    if (result.contains("help")) {
      print_usage(options);
      return 0;
    }

    if (result.contains("version")) {
      std::cout << "instrument-worker " << INSTSERVER_VERSION;

      if (std::string(INSTSERVER_GIT_TAG).size() > 0) {
        std::cout << " (" << INSTSERVER_GIT_TAG << ")";
      }

      if (std::string(INSTSERVER_GIT_COMMIT) != "unknown") {
        std::cout << " [" << std::string(INSTSERVER_GIT_COMMIT).substr(0, 7)
                  << "]";
      }

      std::cout << "\n";
      return 0;
    }

    if (!result.contains("instrument_config") || !result.contains("plugin")) {
      std::cerr << "Missing required arguments\n";
      print_usage(options);
      return 1;
    }

    const std::filesystem::path instrument_config =
        result["instrument_config"].as<std::string>();

    const std::filesystem::path plugin = result["plugin"].as<std::string>();

    std::string log_level_str = result["log-level"].as<std::string>();

    std::ranges::transform(log_level_str, log_level_str.begin(), ::tolower);

    uint8_t log_level = INST_LOG_INFO;

    try {
      log_level = parse_log_level(log_level_str);
    } catch (const std::exception &e) {
      std::cerr << "[WORKER] " << e.what() << "\n";
      return 1;
    }

    InstrumentConfig config;
    try {
      config = load_config(instrument_config);
    } catch (const std::exception &e) {
      std::cerr << "[WORKER] Failed to load config '" << instrument_config
                << "': " << e.what() << "\n";
      return 1;
    }

    const std::string log_file = "worker_" + config.name + ".log";

    inst_log_init(log_file.c_str(), log_level, "instrument", 10 * 1024 * 1024,
                  3);

    std::unordered_map<std::string, Command> instrument_commands;

    const std::filesystem::path api_path =
        instrument_config.parent_path() / config.api_ref;

    try {
      instrument_commands = load_api(api_path);
    } catch (const std::exception &e) {
      LOG_ERROR(config.name.c_str(), "WORKER_MAIN", "Invalid api '%s': %s\n",
                api_path.string().c_str(), e.what());
      return 1;
    }

    LOG_DEBUG(config.name.c_str(), "WORKER_MAIN", "Worker starting");
    LOG_DEBUG(config.name.c_str(), "WORKER_MAIN", "Plugin: %s", plugin.c_str());

    strncpy(g_instrument_name_buf, config.name.c_str(),
            sizeof(g_instrument_name_buf) - 1);
    g_instrument_name_buf[sizeof(g_instrument_name_buf) - 1] = '\0';

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define USING_ASAN 1
#endif
#endif

#ifndef USING_ASAN
    std::signal(SIGSEGV, crash_signal_handler);
    std::signal(SIGABRT, crash_signal_handler);
    std::signal(SIGFPE, crash_signal_handler);
#endif

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
      InstrumentWorker worker(config, plugin.string(), instrument_commands);

      int rc = worker.run();

      LOG_INFO(config.name.c_str(), "WORKER_MAIN", "Worker exited with code %d",
               rc);

      inst_log_flush();
      return rc;

    } catch (const std::exception &e) {
      LOG_ERROR(config.name.c_str(), "WORKER_MAIN", "Fatal error: %s\n",
                e.what());
      inst_log_flush();
      return 1;

    } catch (...) {
      LOG_ERROR(config.name.c_str(), "WORKER_MAIN",
                "Fatal unknown exception\n");
      inst_log_flush();
      return 1;
    }

  } catch (const cxxopts::exceptions::exception &e) {
    std::cerr << "Argument error: " << e.what() << "\n";
    return 1;

  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << "\n";
    return 1;

  } catch (...) {
    std::cerr << "Fatal error: unknown exception\n";
    return 1;
  }
}
