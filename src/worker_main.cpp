#include "instrument-script-server/core/ErrorCodes.hpp"
#include "instrument-script-server/core/Formatter.hpp"
#include "instrument-script-server/core/InstrumentCommand.hpp"
#include "instrument-script-server/core/ParsingTools.hpp"
#include "instrument-script-server/core/PluginLoader.hpp"
#include "instrument-script-server/ipc/IPCMessage.hpp"
#include "instrument-script-server/ipc/SharedQueue.hpp"
#include <algorithm>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cxxopts.hpp>
#include <filesystem>
#include <instrument-data.h>
#include <instrument-log/inst_logging.h>
#include <instrument-plugin.h>
#include <iostream>
#include <mutex>
#include <optional>
#include <plugin-host.h>
#include <queue>
#include <stdexcept>
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

std::string readable_param_types(uint8_t param_type) {
  if (param_type == PARAM_TYPE_INT64) {
    return "int";
  }
  if (param_type == PARAM_TYPE_DOUBLE) {
    return "float";
  }
  if (param_type == PARAM_TYPE_BUFFER) {
    return "buffer";
  }
  if (param_type == PARAM_TYPE_STRING) {
    return "string";
  }
  if (param_type == PARAM_TYPE_BOOL) {
    return "bool";
  }
  return "none";
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
  pcmd->timeout_ms = static_cast<uint32_t>(cmd.timeout.count());
  return pcmd;
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

std::string
expandTemplate(const std::string &input,
               const std::unordered_map<std::string, std::string> &values) {
  std::string result;
  size_t pos = 0;

  while (pos < input.size()) {
    size_t open = input.find('{', pos);

    if (open == std::string::npos) {
      result.append(input, pos, std::string::npos);
      break;
    }

    result.append(input, pos, open - pos);

    size_t close = input.find('}', open);
    if (close == std::string::npos) {
      result.append(input, open, std::string::npos);
      break;
    }

    std::string key = input.substr(open + 1, close - open - 1);

    auto it = values.find(key);
    if (it != values.end()) {
      result += it->second;
    } else {
      result.append(input, open, close - open + 1); // leave unchanged
    }

    pos = close + 1;
  }

  return result;
}
template <size_t N>
inline void safe_c_str_copy(char (&dest)[N], std::string_view src) {
  const size_t bytes_to_copy = std::min(src.size(), N - 1);

  std::memcpy(dest, src.data(), bytes_to_copy);
  dest[bytes_to_copy] = '\0';
}
class InstrumentWorker {
public:
  InstrumentWorker(InstrumentConfig config, const std::string &plugin_path,
                   std::unordered_map<std::string, Command> commands,
                   std::unordered_map<std::string, IO> ios,
                   ChannelGroupData groups)
      : config_(std::move(config)), plugin_path_(plugin_path),
        commands_(std::move(commands)), plugin_(plugin_path),
        ios_(std::move(ios)), groups_(std::move(groups)) {}

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
  std::unordered_map<std::string, IO> ios_;
  ChannelGroupData groups_;
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
      incoming_read_messages_;

  using Incoming = std::variant<ipc::IPCMessage, // SINGLETON
                                uint64_t         // QUEUE (sync token)
                                >;
  std::queue<Incoming>
      queue_of_incoming_messages_; // The next index to pop comings after the
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
    config->baud_rate = config_.baudrate.value_or(9600);
    config->startup_delay = config_.startup_delay.value_or(0);
    copy_cstr(config->custom, config_.custom.value_or(""));
    if (config_.init_commands.has_value()) {
      if (config_.init_commands.value().size() > STARTUP_COMMANDS) {
        log_warn("Too many initialization commands sent to the instrument. "
                 "Expected up to %d commands, but received %d commands. "
                 "Commands are truncated",
                 STARTUP_COMMANDS, config_.init_commands.value().size());
      }

      const size_t loop_count = std::min(config_.init_commands.value().size(),
                                         static_cast<size_t>(STARTUP_COMMANDS));
      for (size_t i = 0; i < loop_count; ++i) {
        auto *base_ptr = (&(config->init_commands));
        auto &row_ref = (*base_ptr)[i];
        copy_cstr(row_ref, config_.init_commands.value()[i]);
      }
      // Sets the remaining_bytes of the array to '\0' for plugin
      if (loop_count < STARTUP_COMMANDS) {
        auto *base_ptr = (&(config->init_commands));
        auto &unused_row_ref = (*base_ptr)[loop_count];
        size_t remaining_bytes =
            (STARTUP_COMMANDS - loop_count) * PLUGIN_MAX_STRING_LEN;
        std::memset(&unused_row_ref, 0, remaining_bytes);
      }
    }

    ErrorCode init_result = plugin_.initialize(config.get());
    if (init_result != ErrorCode::NONE) {
      log_error("Plugin initialization failed: %d\n", init_result);
      return false;
    }

    log_info("Plugin initialized successfully");
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
    log_info("IPC queue connected");
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

    log_info("Shutting down after %llu iters", (unsigned long long)iteration);
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

      std::vector<ipc::VariableWithUnit> plugin_resp{};

      InstrumentCommand stub_cmd{};
      stub_cmd.id = safe_string(first.id.data(), PLUGIN_MAX_STRING_LEN);
      stub_cmd.verb = "unknown";

      send_command_response(first, stub_cmd, plugin_resp,
                            ErrorCode::MISSING_MESSAGE_ID);
      return;
    }

    execute_command(cmd, first);
  }
  uint8_t
  calculate_template_pair(const Variable &var, const Format &fmt,
                          std::pair<std::string, std::string> *out) const {
    auto form = Formatter(fmt);
    out->first = var.name;
    VariableType type = var.type;
    if (type == PARAM_TYPE_INT64) {
      out->second = form.format(var.value.i64_val);
      return 0;
    }
    if (type == PARAM_TYPE_STRING) {
      out->second = form.format(var.value.str_val);
      return 0;
    }
    if (type == PARAM_TYPE_DOUBLE) {
      out->second = form.format(var.value.d_val);
      return 0;
    }
    if (type == PARAM_TYPE_BOOL) {
      out->second = form.format(var.value.b_val);
      return 0;
    }
    if (type == PARAM_TYPE_BUFFER) {
      log_error("Buffer type found for VISA instrument. VISA instruments "
                "cannot use buffers as input");
      return 1;
    }
    log_error("Unsupported type %d found when filling out command template",
              type);
    return 1;
  }
  // If a part of a channel group we can extract a channel from config metadata
  static std::optional<int64_t>
  find_channel(const Command &truth, const InstrumentCommand &incoming) {
    if (!truth.group_name.has_value()) {
      return std::nullopt;
    }
    for (auto actual_parameter : incoming.params) {
      if (actual_parameter.name == truth.group_name.value()) {
        return actual_parameter.value.i64_val;
      }
    }
    return std::nullopt;
  }
  // Returns the unit if set
  std::string apply_offset_and_gain(Variable *subject, const Command truth,
                                    const std::optional<uint64_t> channel) {
    uint8_t type = subject->type;
    std::string name = subject->name;
    if (type != PARAM_TYPE_DOUBLE && type != PARAM_TYPE_BUFFER) {
      return "";
    }
    log_trace("Checking gain and offset");
    std::string index_name;

    if (ios_.contains(name)) {
      IO io = ios_.at(name);
      if (io.role.has_value() && !is_signal_role(io.role.value())) {
        return "";
      }
      index_name = name;
      log_trace("The name to check in the config for the parameter: %s",
                index_name.c_str());
    } else if (truth.group_name.has_value()) {

      auto group_it =
          groups_.channel_group_io_lookup.find(truth.group_name.value());

      if (group_it == groups_.channel_group_io_lookup.end() ||
          !group_it->second.contains(name)) {
        return "";
      }
      if (!channel.has_value()) {
        log_error("Channel-group IO found but no channel supplied");
        return "";
      }

      index_name = truth.group_name.value() + std::to_string(channel.value()) +
                   "_" + name;
      log_trace("The name to check in the config for the parameter is "
                "mangled: %s",
                index_name.c_str());
    } else {
      return "";
    }
    auto config_it = config_.io_config.find(index_name);
    if (config_it == config_.io_config.end()) {
      log_error("IO_config missing entry '%s'", index_name.c_str());

      for (const auto &[key, value] : config_.io_config) {
        log_error("Configured IO: %s", key.c_str());
      }
      return "";
    }

    double offset = config_it->second.offset;
    double scale = config_it->second.scale;
    std::string unit = config_it->second.unit.value_or("");
    if (scale == 0.0) {
      log_error("Scale is exactly zero. No scale will be applied.");
      return "";
    }
    if (std::abs(scale) < std::numeric_limits<double>::epsilon()) {
      log_error("Scale is extremely close to zero %f. No scale will be applied",
                scale);
      return "";
    }

    if (type == PARAM_TYPE_DOUBLE) {
      double old_value = subject->value.d_val;
      double adjusted_value = (old_value - offset) / scale;
      subject->value.d_val = adjusted_value;
      log_debug("Applying offset %f and scale %f for the parameter %s from "
                "%f to %f",
                offset, scale, index_name.c_str(), old_value, adjusted_value);
      log_debug("Applying unit %s for the parameter %s ", unit.c_str(),
                index_name.c_str());
      return unit;
    }
    std::string buffer_id = subject->value.str_val;
    SharedMetadata metadata = {};
    data_manager_get_metadata(buffer_id.c_str(), &metadata);
    if (metadata.type != INST_DATA_FLOAT32 &&
        metadata.type != INST_DATA_FLOAT64) {
      return "";
    }
    log_debug("Applying offset %f and scale %f for the data_buffer %s ", offset,
              scale, index_name.c_str());
    data_manager_add_offset(buffer_id.c_str(), -offset);
    data_manager_multiply_gain(buffer_id.c_str(), 1 / scale);
    log_debug("Applying unit %s for the parameter %s ", unit.c_str(),
              index_name.c_str());
    return unit;
  }
  void apply_min_max(Variable *subject, const IO &config_truth) const {
    uint8_t type = subject->type;
    std::string name = subject->name;
    if (type != PARAM_TYPE_INT64 && type != PARAM_TYPE_DOUBLE) {
      return;
    }
    struct LimitConfig {
      std::optional<std::variant<int64_t, double>> instserver::IO::*member;
      const char *label;
      enum : uint8_t { MIN_LIMIT, MAX_LIMIT } bound_type;
    };
    const std::array<LimitConfig, 2> limits{
        {{.member = &instserver::IO::min,
          .label = "Min",
          .bound_type = LimitConfig::MIN_LIMIT},
         {.member = &instserver::IO::max,
          .label = "Max",
          .bound_type = LimitConfig::MAX_LIMIT}}};
    for (const auto &config : limits) {
      const auto &limit_opt = config_truth.*(config.member);
      if (!limit_opt.has_value()) {
        continue;
      }
      log_trace("Checking %s limit for parameter %s", config.label,
                name.c_str());

      std::visit(
          [&](const auto &limit_val) {
            if (type == PARAM_TYPE_DOUBLE) {
              const auto limit = static_cast<double>(limit_val);
              double old_value = subject->value.d_val;
              double adjusted_value =
                  (config.bound_type == LimitConfig::MIN_LIMIT)
                      ? std::max(old_value, limit)
                      : std::min(old_value, limit);
              subject->value.d_val = adjusted_value;
              log_debug("Applying %s limit to parameter %s from %f to %f",
                        config.label, name.c_str(), old_value, adjusted_value);
            } else if (type == PARAM_TYPE_INT64) {
              const auto limit = static_cast<int64_t>(limit_val);
              int64_t old_value = subject->value.i64_val;
              int64_t adjusted_value =
                  (config.bound_type == LimitConfig::MIN_LIMIT)
                      ? std::max(old_value, limit)
                      : std::min(old_value, limit);
              subject->value.i64_val = adjusted_value;
              log_debug("Applying %s limit to parameter %s from %d to %d",
                        config.label, name.c_str(), old_value, adjusted_value);
            }
          },
          *limit_opt);
    }
  }
  void apply_precision(Variable *subject, const IO &truth) const {
    uint8_t type = subject->type;
    std::string name = subject->name;
    if (type == PARAM_TYPE_DOUBLE && truth.precision.resolution.has_value()) {
      log_trace("Editing parameter %s precision due to resolution",
                name.c_str());
      const double res = truth.precision.resolution.value();
      if (res > 0.0) {
        double old_value = subject->value.d_val;
        double adjusted_value = std::round(old_value / res) * res;
        log_debug("Applying prevision %f for the parameter %s from "
                  "%f to %f",
                  res, name.c_str(), old_value, adjusted_value);
        subject->value.d_val = adjusted_value;
      }
    }
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

    size_t actual_size = cmd.params.size();

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

    if (config_.api_type.type == instserver::VISA &&
        !command.temp.has_value()) {
      log_error("Commmand %s found without template for VISA instrument",
                command.name.c_str());
      return;
    }

    std::unordered_map<std::string, std::string>
        template_values; // for VISA use
    std::optional<int64_t> channel = find_channel(command, cmd);
    for (size_t i = 0; i < cmd.params.size(); i++) {
      const auto expected_parameter = command.parameters[i];
      Variable actual_parameter = cmd.params[i];

      auto expected_name = expected_parameter.name;
      const std::string_view actual_name = actual_parameter.name;

      if (actual_name != expected_name) {
        log_error("Config command %s parameter name mismatch at index %d: "
                  "expected='%s', got='%s'",
                  cmd.verb.c_str(), i, expected_name.c_str(),
                  cmd.params[i].name);
        return;
      }

      auto expected_type = expected_parameter.type;
      auto actual_type = actual_parameter.type;

      if (actual_type != expected_type) {
        log_error("Config command %s parameter type mismatch at index %d: "
                  "expected='%d', got='%d'",
                  cmd.verb.c_str(), i, expected_type, actual_type);
        return;
      }
      apply_offset_and_gain(&actual_parameter, command, channel);
      apply_min_max(&actual_parameter, expected_parameter);
      apply_precision(&actual_parameter, expected_parameter);

      // push back a fixed template
      if (config_.api_type.type == instserver::VISA) {
        auto pair = std::make_pair<std::string, std::string>("", "");
        if (calculate_template_pair(actual_parameter,
                                    command.parameters[i].form, &pair) != 0U) {
          return;
        }
        template_values.insert(pair);
      }
    }
    log_debug("All of the parameters types and names match the API");
    std::unique_ptr<PluginCommand> pcmd = to_plugin_command(cmd);
    // If VISA instrument we need to fix the command to be the proper command
    if (config_.api_type.type == instserver::VISA) {
      safe_c_str_copy(pcmd->command,
                      expandTemplate(command.temp.value(), template_values));
      log_debug("Prepared a template for the instrument plugin: %s",
                pcmd->command);
    }
    auto expected_returns_size = command.returns.size();
    PluginResponse *plugin_resp =
        plugin_response_create_with_capacity(expected_returns_size);
    pcmd->is_query = false;
    if (expected_returns_size > 0) {
      pcmd->is_query = true;
    }

    // ---- normal execution ----
    ErrorCode exec_result = plugin_.execute_command(pcmd.get(), plugin_resp);
    log_info("Command executed: result=%u success=%s",
             static_cast<unsigned>(exec_result), no_error(exec_result).data());

    // ---- response validation ----
    size_t actual_resp_count = plugin_response_count(plugin_resp);
    if (actual_resp_count != expected_returns_size) {
      log_error("Command %s returns count mismatch: expected '%d', got '%d'",
                cmd.verb.c_str(), expected_returns_size, actual_resp_count);
      return;
    }
    std::vector<IO> expected_returns = command.returns;
    std::vector<ipc::VariableWithUnit> actual_returns;
    size_t actual_returns_size = actual_returns.size();
    actual_returns.reserve(actual_resp_count);
    for (size_t i = 0; i < actual_resp_count; ++i) {
      ipc::VariableWithUnit value{.var = *plugin_response_get(plugin_resp, i)};

      const std::string unit = expected_returns[i].unit.value_or("");

      std::strncpy(value.unit.data(), unit.c_str(),
                   instserver::ipc::MAX_UNIT_LEN - 1);
      value.unit[instserver::ipc::MAX_UNIT_LEN - 1] = '\0';

      actual_returns.push_back(value);
    }
    int validated_response_count = 0;
    for (size_t i = 0; i < expected_returns.size(); i++) {
      const IO &expected_return = expected_returns[i];
      ipc::VariableWithUnit actual_return = actual_returns[i];
      log_debug("Command %s return type for name %s: expected '%s', got '%s'",
                cmd.verb.c_str(), expected_return.name.c_str(),
                readable_param_types(expected_return.type).c_str(),
                readable_param_types(actual_return.var.type).c_str());
      if (expected_return.type != actual_return.var.type) {
        log_error("Command %s return type for name %s mismatch: expected "
                  "'%s', got '%s'",
                  cmd.verb.c_str(), expected_return.name.c_str(),
                  readable_param_types(expected_return.type).c_str(),
                  readable_param_types(actual_return.var.type).c_str());
        return;
      }
      validated_response_count++;
    }
    if (validated_response_count != expected_returns_size) {
      log_error("Command %s returns count mismatch: expected '%d', "
                "got '%d'",
                cmd.verb.c_str(), expected_returns_size, actual_returns_size);
      return;
    }
    for (size_t i = 0; i < expected_returns.size(); i++) {
      ipc::VariableWithUnit actual_return = actual_returns[i];
      std::string unit =
          apply_offset_and_gain(&actual_return.var, command, channel);
      if (unit != "") {
        std::strncpy(actual_return.unit.data(), unit.c_str(),
                     instserver::ipc::MAX_UNIT_LEN - 1);
        actual_return.unit[instserver::ipc::MAX_UNIT_LEN - 1] = '\0';
      }
    }

    send_command_response(msg, cmd, actual_returns, exec_result);
    param_storage_free(pcmd->params);
    plugin_response_free(plugin_resp);

    // ---- sync handling ----
    if (!cmd.sync_token.has_value()) {
      return;
    }
    uint64_t token = cmd.sync_token.value();

    send_sync_ack(msg, token);
  }
  void
  send_command_response(const ipc::IPCMessage &msg,
                        const InstrumentCommand &cmd,
                        const std::vector<ipc::VariableWithUnit> &plugin_resp,
                        ErrorCode error_code) {
    InstrumentCommandResponse resp{};
    resp.error_code = error_code;
    resp.id = std::move(safe_string(msg.id.data(), PLUGIN_MAX_STRING_LEN));
    resp.returns = plugin_resp;
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
    std::unordered_map<std::string, IO> ios = load_io(api_path);
    ChannelGroupData groups = load_channel_groups(api_path);

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
      InstrumentWorker worker(config, plugin.string(), instrument_commands, ios,
                              groups);

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
