#include "instrument-script-server/client/instrument-server-client.hpp"
#include "instserver/server/v1/daemon_messages.pb.h"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <google/protobuf/util/json_util.h>
#include <instrument-data.h>
#include <instrument-log/inst_logging.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#ifndef INSTSERVER_VERSION
#define INSTSERVER_VERSION "v0.0.0"
#endif
#ifndef INSTSERVER_GIT_TAG
#define INSTSERVER_GIT_TAG ""
#endif
#ifndef INSTSERVER_GIT_COMMIT
#define INSTSERVER_GIT_COMMIT "unknown"
#endif
namespace {
struct CLIOutput {
  bool json_mode = false;

  std::vector<std::string> messages;
  std::vector<std::string> errors;
  std::vector<nlohmann::json> outputs;

  void message(const std::string &msg) { messages.push_back(msg); }

  void error(const std::string &msg) { errors.push_back(msg); }

  template <typename ProtoT> void output_proto_message(const ProtoT &msg) {
    std::string json;

    auto status = google::protobuf::util::MessageToJsonString(msg, &json);

    if (!status.ok()) {
      errors.push_back("Failed to serialize protobuf to JSON: " +
                       std::string(status.message()));
      return;
    }

    outputs.push_back(nlohmann::json::parse(json));
  }

  [[nodiscard]] int emit() const {
    if (json_mode) {
      nlohmann::json j;

      j["ok"] = errors.empty();

      if (!messages.empty()) {
        j["message"] = messages;
      }

      if (!errors.empty()) {
        j["error"] = errors;
      }

      if (!outputs.empty()) {
        j["output"] = outputs;
      }

      std::cout << j.dump(2) << "\n";
    } else {
      for (const auto &msg : messages) {
        std::cout << msg << "\n";
      }

      for (const auto &err : errors) {
        std::cerr << err << "\n";
      }
    }

    return errors.empty() ? 0 : 1;
  }
};

struct CliArgs {
  std::span<char *> argv;
  int start = 1;

  CliArgs(int argc, char **argv_, int start_ = 1)
      : argv(argv_, static_cast<size_t>(argc)), start(start_) {}

  [[nodiscard]] std::span<char *> args() const { return argv.subspan(start); }

  [[nodiscard]] auto args_sv() const {
    return argv.subspan(start) | std::views::transform([](const char *arg) {
             return std::string_view(arg);
           });
  }
  [[nodiscard]] std::string_view at(size_t i) const {
    if (i >= argv.size()) {
      throw std::out_of_range("CliArgs index out of range");
    }
    return argv[i];
  }

  [[nodiscard]] bool has_flag(std::string_view name) const {
    const std::string key = "--" + std::string{name};

    return std::ranges::any_of(
        args(), [&](const char *arg) { return std::string_view(arg) == key; });
  }

  [[nodiscard]] std::string
  get_option(std::string_view name, bool require_value = true,
             std::string_view default_value = "") const {

    const std::string key = "--" + std::string{name};
    auto tail = args();

    for (size_t i = 0; i < tail.size(); ++i) {
      if (std::string_view(tail[i]) == key) {
        if (i + 1 < tail.size()) {
          return tail[i + 1];
        }
        return require_value ? "" : std::string(default_value);
      }
    }

    return std::string(default_value);
  }
};
std::optional<uint32_t> to_int(std::string_view sv) {
  uint32_t value{};
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
  if (ec != std::errc{}) {
    return std::nullopt;
  }
  return value;
}
using instserver::server::v1::VariableValue;

// ---- string_view -> int64 ----
std::optional<int64_t> to_int64(std::string_view sv) {
  int64_t out{};
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), out);
  if (ec != std::errc{}) {
    return std::nullopt;
  }
  return out;
}

// ---- string_view -> double ----
std::optional<double> to_double(std::string_view sv) {
  try {
    return std::stod(std::string(sv));
  } catch (...) {
    return std::nullopt;
  }
}

// ---- key=value parsing ----
std::optional<std::pair<std::string, VariableValue>>
parse_global_kv(std::string_view input) {
  auto pos = input.find('=');
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }

  std::string key(input.substr(0, pos));
  std::string_view value = input.substr(pos + 1);

  VariableValue v;

  if (auto i = to_int64(value)) {
    v.set_i(*i);
    return {{key, v}};
  }

  if (auto d = to_double(value)) {
    v.set_d(*d);
    return {{key, v}};
  }

  if (value == "true") {
    v.set_b(true);
    return {{key, v}};
  }
  if (value == "false") {
    v.set_b(false);
    return {{key, v}};
  }

  v.set_s(std::string(value));
  return {{key, v}};
}

// ---- JSON -> VariableValue ----
void json_to_variable(const nlohmann::json &j, VariableValue &out) {
  if (j.is_null()) {
    out.set_is_nil(true);
  } else if (j.is_boolean()) {
    out.set_b(j.get<bool>());
  } else if (j.is_number_integer()) {
    out.set_i(j.get<int64_t>());
  } else if (j.is_number_float()) {
    out.set_d(j.get<double>());
  } else if (j.is_string()) {
    out.set_s(j.get<std::string>());
  } else if (j.is_array()) {
    if (j.empty()) {
      out.mutable_m_array(); // fallback
      return;
    }

    if (j[0].is_number_integer()) {
      auto *arr = out.mutable_i_array();
      for (const auto &el : j) {
        arr->add_values(el.get<int64_t>());
      }
    } else if (j[0].is_number_float()) {
      auto *arr = out.mutable_d_array();
      for (const auto &el : j) {
        arr->add_values(el.get<double>());
      }
    } else if (j[0].is_boolean()) {
      auto *arr = out.mutable_b_array();
      for (const auto &el : j) {
        arr->add_values(el.get<bool>());
      }
    } else if (j[0].is_string()) {
      auto *arr = out.mutable_s_array();
      for (const auto &el : j) {
        arr->add_values(el.get<std::string>());
      }
    } else {
      auto *arr = out.mutable_m_array();
      for (const auto &el : j) {
        VariableValue tmp;
        json_to_variable(el, tmp);
        *arr->add_values() = tmp;
      }
    }
  } else if (j.is_object()) {
    auto *map = out.mutable_m_map()->mutable_values();
    for (const auto &[k, v] : j.items()) {
      VariableValue tmp;
      json_to_variable(v, tmp);
      (*map)[k] = tmp;
    }
  }
}
struct ParsedGlobals {
  std::vector<std::string> kv;
  std::optional<std::string> json;
};

std::optional<ParsedGlobals> parse_globals(CliArgs &args, CLIOutput &out) {
  ParsedGlobals result;

  constexpr std::string_view GLOBAL_PREFIX = "--global=";
  constexpr std::string_view GLOBAL_FLAG = "--global";
  constexpr std::string_view GLOBALS_JSON_PREFIX = "--globals-json=";

  auto tail = args.args();

  for (size_t i = 0; i < tail.size(); ++i) {
    std::string_view sv(tail[i]);

    if (sv.starts_with(GLOBAL_PREFIX)) {
      sv.remove_prefix(GLOBAL_PREFIX.size());
      result.kv.emplace_back(sv);
    } else if (sv == GLOBAL_FLAG) {
      if (i + 1 >= tail.size()) {
        out.error("Error: --global requires value (key=value)");
        return std::nullopt;
      }
      result.kv.emplace_back(tail[i + 1]);
      ++i;
    } else if (sv.starts_with(GLOBALS_JSON_PREFIX)) {
      sv.remove_prefix(GLOBALS_JSON_PREFIX.size());
      result.json = std::string(sv);
    }
  }

  return result;
}
bool apply_globals(const ParsedGlobals &g,
                   google::protobuf::Map<std::string, VariableValue> &globals,
                   CLIOutput &out) {

  for (const auto &kv : g.kv) {
    auto parsed = parse_global_kv(kv);
    if (!parsed) {
      out.error("Invalid --global: " + kv);
      return false;
    }
    globals[parsed->first] = parsed->second;
  }

  if (g.json) {
    try {
      auto j = nlohmann::json::parse(*g.json);
      for (const auto &[k, v] : j.items()) {
        VariableValue val;
        json_to_variable(v, val);
        globals[k] = val;
      }
    } catch (const std::exception &e) {
      out.error(std::string("Invalid JSON: ") + e.what());
      return false;
    }
  }

  return true;
}
uint16_t get_port() {
  const char *env = std::getenv("INSTRUMENT_SCRIPT_SERVER_RPC_PORT");
  if (env == nullptr) {
    return 8555;
  }
  return static_cast<uint16_t>(std::stoi(env));
}
template <typename Fn> int with_client(CLIOutput &out, Fn &&fn) {
  instserver::client::InstrumentServerClient client(get_port());
  if (!client.is_daemon_running()) {
    out.error("Daemon is not running. Please start the daemon first.");
    return out.emit();
  }
  fn(client);
  return out.emit();
}
template <typename Fn>
auto call_rpc(CLIOutput &out, Fn &&fn) -> std::optional<decltype(fn())> {

  auto resp = fn();
  out.output_proto_message(resp);
  if (!resp.standard_response().ok()) {
    if (resp.standard_response().has_error()) {
      out.error(resp.standard_response().error().message());
    } else {
      out.error("RPC failed");
    }
    return std::nullopt;
  }
  return resp;
}

// Returns the path to instrument-script-server-daemon, preferring co-location
// with this binary (argv[0]), then falling back to the bare name (PATH lookup).
std::string get_daemon_path(const char *argv0) {
  if (argv0 != nullptr) {
    std::filesystem::path self(argv0);
    auto sibling = self.parent_path() / "instrument-script-server-daemon";
    if (std::filesystem::exists(sibling)) {
      return sibling.string();
    }
  }
  return "instrument-script-server-daemon";
}

std::string readable_datatype(uint8_t type) {
  switch (type) {
  case (INST_DATA_UINT8):
    return "uint8_t";
  case (INST_DATA_FLOAT32):
    return "float32";
  case (INST_DATA_FLOAT64):
    return "float64";
  case (INST_DATA_INT32):
    return "int32";
  case (INST_DATA_INT64):
    return "int64";
  case (INST_DATA_UINT32):
    return "uint32_t";
  case (INST_DATA_UINT64):
    return "uint64_t";
  default:
    return "unknown";
  };
}
void print_usage() {
  std::cout <<
      R"(Usage:
  instrument-script-server <command> [options]

DAEMON
  daemon start [--log-level <level>]   Start daemon (default: info)
  daemon stop                          Stop daemon
  daemon status                        Show daemon status

INSTRUMENT
  inst start <config>                       Start instrument
         [--plugin <path>]
         [--log-level <level>]

  inst stop <name>                          Stop instrument
  inst status <name>                        Query instrument
  inst list                                 List running instruments

MEASUREMENT
  measure <script>                     Run measurement
         [--global key=value]          Set simple globals (repeatable)
         [--globals-json <json>]       Set structured globals

JOBS
  job list                             List jobs
  job cancel <job-id>                  Cancel job
  job status <job-id>                  Job status
  job measure <script>                 Queue measurement
         [--global key=value]
         [--globals-json <json>]
  job result <job-id>                  Get result

BUFFERS
  buffer list                          List shared buffers
  buffer metadata <id>                 Buffer metadata
  buffer read <id>                     Read buffer contents
  buffer release <id>                  Release buffer

UTILITIES
  discover [paths...]                  Discover plugins

OPTIONS
  -h, --help       Show help
  -v, --version    Show version
  --json           JSON output

EXAMPLE WORKFLOW
  instrument-script-server daemon start
  instrument-script-server start dac1.yaml
  instrument-script-server start dmm1.yaml
  instrument-script-server start scope1.yaml --plugin ./custom.so
  instrument-script-server measure my_measurement.lua
  instrument-script-server job measure test.lua \
    --global voltage=3.3 \
    --global enabled=true
  instrument-script-server job measure test.lua \
    --globals-json '{"waveform":[1,2,3],"gain":5}'
  instrument-script-server list
  instrument-script-server status DAC1
  instrument-script-server stop DAC1
  instrument-script-server daemon stop
)";
}

enum class ISS_CLI_Command : std::uint8_t {
  DAEMON,
  JOB,
  INST,
  BUFFER,
  MEASURE,
  DISCOVER,
  STATUS,
  HELP,
  HELP_SHORT,
  VERSION,
  VERSION_SHORT,
  UNKNOWN
};

struct CommandEntry {
  ISS_CLI_Command cmd;
  std::string_view name;
};

constexpr std::array<CommandEntry, 11> command_table{
    {{.cmd = ISS_CLI_Command::DAEMON, .name = "daemon"},
     {.cmd = ISS_CLI_Command::INST, .name = "inst"},
     {.cmd = ISS_CLI_Command::BUFFER, .name = "buffer"},
     {.cmd = ISS_CLI_Command::MEASURE, .name = "measure"},
     {.cmd = ISS_CLI_Command::STATUS, .name = "status"},
     {.cmd = ISS_CLI_Command::HELP, .name = "--help"},
     {.cmd = ISS_CLI_Command::HELP_SHORT, .name = "-h"},
     {.cmd = ISS_CLI_Command::VERSION, .name = "--version"},
     {.cmd = ISS_CLI_Command::VERSION_SHORT, .name = "-v"},
     {.cmd = ISS_CLI_Command::DISCOVER, .name = "discover"},
     {.cmd = ISS_CLI_Command::JOB, .name = "job"}}};

constexpr std::string_view to_string(ISS_CLI_Command cmd) {
  for (const auto &entry : command_table) {
    if (entry.cmd == cmd) {
      return entry.name;
    }
  }
  return "unknown";
}

constexpr ISS_CLI_Command parse_command(std::string_view str) {
  for (const auto &entry : command_table) {
    if (entry.name == str) {
      return entry.cmd;
    }
  }
  return ISS_CLI_Command::UNKNOWN;
}
enum class SUB_DAEMON : std::uint8_t { START, STOP, STATUS, UNKNOWN };
struct SubDaemonEntry {
  SUB_DAEMON cmd;
  std::string_view name;
};
constexpr SUB_DAEMON parse_sub_daemon(std::string_view s) {
  if (s == "status") {
    return SUB_DAEMON::STATUS;
  }
  if (s == "start") {
    return SUB_DAEMON::START;
  }
  if (s == "stop") {
    return SUB_DAEMON::STOP;
  }
  return SUB_DAEMON::UNKNOWN;
}
enum class SUB_BUFFER : std::uint8_t { LIST, METADATA, READ, RELEASE, UNKNOWN };
constexpr SUB_BUFFER parse_sub_buffer(std::string_view s) {
  if (s == "list") {
    return SUB_BUFFER::LIST;
  }
  if (s == "metadata") {
    return SUB_BUFFER::METADATA;
  }
  if (s == "read") {
    return SUB_BUFFER::READ;
  }
  if (s == "release") {
    return SUB_BUFFER::RELEASE;
  }

  return SUB_BUFFER::UNKNOWN;
}
enum class SUB_INST : std::uint8_t { START, STOP, STATUS, LIST, UNKNOWN };
constexpr SUB_INST parser_sub_inst(std::string_view s) {
  if (s == "start") {
    return SUB_INST::START;
  }
  if (s == "stop") {
    return SUB_INST::STOP;
  }
  if (s == "status") {
    return SUB_INST::STATUS;
  }
  if (s == "list") {
    return SUB_INST::LIST;
  }
  return SUB_INST::UNKNOWN;
}

enum class SUB_JOB : std::uint8_t {
  LIST,
  CANCEL,
  STATUS,
  MEASURE,
  RESULT,
  UNKNOWN
};
struct SubJobEntry {
  SUB_JOB cmd;
  std::string_view name;
};
constexpr SUB_JOB parse_sub_job(std::string_view s) {
  if (s == "measure") {
    return SUB_JOB::MEASURE;
  }
  if (s == "status") {
    return SUB_JOB::STATUS;
  }
  if (s == "result") {
    return SUB_JOB::RESULT;
  }
  if (s == "cancel") {
    return SUB_JOB::CANCEL;
  }
  if (s == "list") {
    return SUB_JOB::LIST;
  }
  return SUB_JOB::UNKNOWN;
}
constexpr uint8_t parse_log_level(const std::string &s) {
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
constexpr int MIN_MEASURE_DELAY_MS = 10;
} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  CLIOutput out{};
  CliArgs args(argc, argv, 0);
  out.json_mode = args.has_flag("json");
  std::string_view command = args.at(1);
  try {
    switch (parse_command(command)) {
    case ISS_CLI_Command::DAEMON: {
      // subcommand is positional 0
      if (argc < 3) {
        out.error("Usage: instrument-script-server daemon <start|stop|status> "
                  "[--json]");
        return out.emit();
      }
      std::string_view action = args.at(2);
      switch (parse_sub_daemon(action)) {
      case SUB_DAEMON::START: {
        std::string log_level = args.get_option("log-level", true, "info");

        // Refuse to start a second daemon process if one is already running.
        // Use the client-side gRPC check so we don't depend on server headers
        // and so we correctly detect a running daemon even if PID file cleanup
        // is racing (the gRPC check reflects actual daemon liveness).
        try {
          instserver::client::InstrumentServerClient client(get_port());
          if (client.is_daemon_running()) {
            out.error("Daemon is already running on port " +
                      std::to_string(get_port()));
            return out.emit();
          }
        } catch (...) {
          // OK if it throws → means not running
        }
#ifdef _WIN32
        std::string cmd = "instrument-script-server-daemon.exe";

        if (!log_level.empty()) {
          cmd += " --log-level " + log_level;
        }

        STARTUPINFOA si{};
        PROCESS_INFORMATION pi{};
        si.cb = sizeof(si);

        std::string exe = get_daemon_path(argv[0]);

        std::vector<char> cmd_buf(exe.begin(), exe.end());
        cmd_buf.push_back('\0');

        BOOL ok = CreateProcessA(exe.c_str(), cmd_buf.data(), NULL, NULL, FALSE,
                                 DETACHED_PROCESS | CREATE_NO_WINDOW, NULL,
                                 NULL, &si, &pi);

        if (!ok) {
          out.error("Child daemon launch failed");
          return out.emit();
        }

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

#else
        pid_t child_pid = fork();

        if (child_pid < 0) {
          out.error("Child daemon fork failed");
          return out.emit();
        }

        if (child_pid == 0) {
          // Child process
          setsid();

          int devnull = open("/dev/null", O_RDWR);
          if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
          }

          std::string daemon_path = get_daemon_path(argv[0]);
          execl(daemon_path.c_str(), daemon_path.c_str(), "--log-level",
                log_level.empty() ? "info" : log_level.c_str(), nullptr);

          _exit(1); // exec failed
        }
#endif
        bool running = false;
        for (int i = 0; i < 20; ++i) {
          try {
            instserver::client::InstrumentServerClient client(get_port());
            instserver::server::v1::DaemonStatusRequest req;
            auto resp = client.daemon_status(req);
            if (resp.running()) {
              running = true;
              out.output_proto_message(resp);
              break;
            }
          } catch (...) {
            // still starting → ignore
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!running) {
          out.error("Daemon failed to start");
          return out.emit();
        }
        out.message("Daemon started");
        return out.emit();
      }
      case SUB_DAEMON::STOP: {
        return with_client(out, [&](auto &client) {
          instserver::client::v1::DaemonStop req;
          auto resp = client.stop_daemon(req);
          out.output_proto_message(resp);
          if (!resp.ok()) {
            if (resp.has_error()) {
              out.error(resp.error().message());
            } else {
              out.error("RPC failed");
            }
            return;
          }
          out.message("Daemon stopped");
        });
      }
      case SUB_DAEMON::STATUS: {
        instserver::client::InstrumentServerClient client(get_port());
        instserver::client::v1::DaemonStatusRequest req;
        auto resp = client.daemon_status(req);
        out.output_proto_message(resp);
        if (!resp.standard_response().ok()) {
          out.error("Invalid response from daemon");
          return out.emit();
        }
        if (resp.running()) {
          out.message("Daemon is running (PID: " + std::to_string(resp.pid()) +
                      ")");
        } else {
          // non-running = error
          out.error("Daemon is not running");
        }
        return out.emit();
      }
      case SUB_DAEMON::UNKNOWN:
      default: {
        out.error("Usage: instrument-script-server daemon <start|stop|status> "
                  "[--json]");
        return out.emit();
      }
      } // end case ISS_CLI_Command::DAEMON
    }
    case ISS_CLI_Command::INST: {
      auto action = args.at(2);
      if (argc < 3) {
        out.error("Error: unknown inst command: " + std::string(action) +
                  "\n\n"
                  "Valid commands:\n"
                  "  start <script>\n"
                  "  stop <name>\n"
                  "  status <name>\n"
                  "  list");
        return out.emit();
      }
      switch (parser_sub_inst(action)) {

      case SUB_INST::START: {
        if (argc < 4) {
          out.error("Usage: start <config> [--json]");
          return out.emit();
        }
        return with_client(out, [&](auto &client) {
          instserver::client::v1::StartInstrumentRequest req;
          req.set_config_path(std::string(args.at(3)));
          req.set_plugin_path(args.get_option("plugin"));
          req.set_log_level(args.get_option("log-level", true, "info"));
          auto resp =
              call_rpc(out, [&] { return client.start_instrument(req); });
          if (!resp.has_value()) {
            return;
          }
          out.message("Instrument started successfully");
        });
      }
      case SUB_INST::STOP: {
        if (argc < 4) {
          out.error("Error: stop requires instrument name\n"
                    "Usage: instrument-script-server stop <name> [--json]");
          return out.emit();
        }
        return with_client(out, [&](auto &client) {
          instserver::client::v1::StopInstrumentRequest req;
          req.set_instrument_name(std::string(args.at(3)));
          auto resp =
              call_rpc(out, [&] { return client.stop_instrument(req); });
          if (!resp.has_value()) {
            return;
          }
          out.message("Stopped instrument: " + std::string(args.at(3)));
        });
      }
      case SUB_INST::STATUS: {
        if (argc < 4) {
          out.error("Usage: status <name> [--json]");
          return out.emit();
        }
        return with_client(out, [&](auto &client) {
          instserver::client::v1::InstrumentStatusRequest req;
          req.set_instrument_name(args.at(3));
          auto resp =
              call_rpc(out, [&] { return client.instrument_status(req); });
          if (!resp.has_value()) {
            return;
          }
          out.message("Instrument: " + std::string(args.at(3)));
          if (resp.value().has_stats()) {
            out.message("Commands sent: " +
                        std::to_string(resp.value().stats().commands_sent()));
          }
        });
      }
      case SUB_INST::LIST: {
        return with_client(out, [&](auto &client) {
          instserver::client::v1::ListInstrumentsRequest req;
          auto resp =
              call_rpc(out, [&] { return client.list_instruments(req); });
          if (!resp.has_value()) {
            return;
          }
          if (resp.value().instrument_name().empty()) {
            out.error("No instruments running");
          } else {
            out.message("Running instruments:");
            for (const auto &name : resp.value().instrument_name()) {
              out.message("  " + name);
            }
          }
        });
      }
      case SUB_INST::UNKNOWN:
      default: {
        out.error("Error: unknown inst command: " + std::string(action) +
                  "\n\n"
                  "Valid commands:\n"
                  "  start <script>\n"
                  "  stop <name>\n"
                  "  status <name>\n"
                  "  list");
        return out.emit();
      }
      }
    }
    case ISS_CLI_Command::MEASURE: {
      if (argc < 3) {
        out.error("Error: missing script path\n\n"
                  "Usage:\n"
                  "  instrument-script-server measure <script>\n"
                  "    [--global key=value] (repeatable)\n"
                  "    [--globals-json <json>]");
        return out.emit();
      }

      auto parsed_globals = parse_globals(args, out);
      if (!parsed_globals) {
        return out.emit();
      }

      return with_client(out, [&](auto &client) {
        instserver::client::v1::MeasureJobRequest req;
        req.set_script_path(std::string(args.at(2)));

        auto *globals = req.mutable_globals()->mutable_map();

        if (!apply_globals(*parsed_globals, *globals, out)) {
          return;
        }

        auto resp = call_rpc(out, [&] { return client.measure_job(req); });
        if (!resp) {
          return;
        }

        uint32_t job_id = resp->job_id();

        std::this_thread::sleep_for(
            std::chrono::milliseconds(MIN_MEASURE_DELAY_MS));

        while (true) {
          instserver::client::v1::JobStatusRequest status_req;
          status_req.set_job_id(job_id);

          auto status_resp =
              call_rpc(out, [&] { return client.job_status(status_req); });

          if (!status_resp) {
            return;
          }

          if (!status_resp->has_job()) {
            out.error("Invalid job status response");
            return;
          }

          auto status = status_resp->job().status();

          if (status == instserver::client::v1::JobStatus::JOB_STATUS_FAILED ||
              status ==
                  instserver::client::v1::JobStatus::JOB_STATUS_CANCELLED) {
            out.error("Job failed or was cancelled");
            return;
          }

          if (status !=
              instserver::client::v1::JobStatus::JOB_STATUS_COMPLETED) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(MIN_MEASURE_DELAY_MS));
            continue;
          }

          instserver::client::v1::MeasureJobResultRequest result_req;
          result_req.set_job_id(job_id);

          auto result_resp = call_rpc(
              out, [&] { return client.measure_job_result(result_req); });

          if (!result_resp) {
            return;
          }

          out.message("Measurement complete");

          out.message("  Status: " + std::to_string(result_resp->status()));

          if (result_resp->results().empty()) {
            out.message("  No results");
            return;
          }

          for (const auto &cmd : result_resp->results()) {
            out.message("  Command:");
            out.message("    Instrument: " + cmd.instrument_name());
            out.message("    Verb: " + cmd.verb());

            for (const auto &param : cmd.param()) {
              std::string value = "<complex>";
              const auto &v = param.value();

              if (v.has_i()) {
                value = std::to_string(v.i());
              } else if (v.has_d()) {
                value = std::to_string(v.d());
              } else if (v.has_b()) {
                value = v.b() ? "true" : "false";
              } else if (v.has_s()) {
                value = v.s();
              }

              out.message("      " + param.name() + " = " + value);
            }
          }

          return;
        }
      });
    }
    case ISS_CLI_Command::JOB: {
      if (argc < 3) {
        out.error("Error: missing job subcommand\n\n"
                  "Usage:\n"
                  "  instrument-script-server job "
                  "<measure|list|cancel|status|result>");
        return out.emit();
      }
      auto require_int = [&](std::string_view sv,
                             std::string_view name) -> std::optional<uint32_t> {
        auto val = to_int(sv);
        if (!val) {
          out.error("Error: invalid " + std::string(name) + ": " +
                    std::string(sv));
        }
        return val;
      };

      auto action = args.at(2);

      switch (parse_sub_job(action)) {

      case SUB_JOB::MEASURE: {
        if (argc < 4) {
          out.error("Error: missing script path\n\n"
                    "Usage:\n"
                    "  instrument-script-server job measure <script>\n"
                    "    [--global key=value] (repeatable)\n"
                    "    [--globals-json <json>]");
          return out.emit();
        }

        auto parsed_globals = parse_globals(args, out);
        if (!parsed_globals) {
          return out.emit();
        }

        return with_client(out, [&](auto &client) {
          instserver::client::v1::MeasureJobRequest req;
          req.set_script_path(std::string(args.at(3)));

          auto *globals_ptr = req.mutable_globals()->mutable_map();

          if (!apply_globals(*parsed_globals, *globals_ptr, out)) {
            return;
          }

          auto resp = call_rpc(out, [&] { return client.measure_job(req); });
          if (!resp) {
            return;
          }

          out.message("Enqueued measurement (job_id=" +
                      std::to_string(resp->job_id()) + ")");
        });
      }

      case SUB_JOB::STATUS: {
        if (argc < 4) {
          out.error("Error: missing job id\n\n"
                    "Usage:\n"
                    "  instrument-script-server job status <job_id>");
          return out.emit();
        }

        auto jid = require_int(args.at(3), "job id");
        if (!jid) {
          return out.emit();
        }

        return with_client(out, [&](auto &client) {
          instserver::client::v1::JobStatusRequest req;
          req.set_job_id(*jid);

          auto resp = call_rpc(out, [&] { return client.job_status(req); });
          if (!resp) {
            return;
          }

          const auto &job = resp->job();

          out.message("Job " + std::to_string(*jid));
          out.message("  Status: " + std::to_string(job.status()));
        });
      }

      case SUB_JOB::LIST: {
        return with_client(out, [&](auto &client) {
          instserver::client::v1::JobListRequest req;
          auto resp = call_rpc(out, [&] { return client.job_list(req); });
          if (!resp) {
            return;
          }

          if (resp->jobs().empty()) {
            out.message("No jobs");
            return;
          }

          out.message("Jobs:");
          for (const auto &[id, job] : resp->jobs()) {
            out.message("  " + std::to_string(id) +
                        " (status=" + std::to_string(job.status()) + ")");
          }
        });
      }

      case SUB_JOB::CANCEL: {
        if (argc < 4) {
          out.error("Error: missing job id\n\n"
                    "Usage:\n"
                    "  instrument-script-server job cancel <job_id>");
          return out.emit();
        }

        auto jid = require_int(args.at(3), "job id");
        if (!jid) {
          return out.emit();
        }

        return with_client(out, [&](auto &client) {
          instserver::client::v1::CancelJobRequest req;
          req.set_job_id(*jid);

          auto resp = call_rpc(out, [&] { return client.cancel_job(req); });
          if (!resp) {
            return;
          }

          out.message("Cancelled job " + std::to_string(*jid));
        });
      }

      case SUB_JOB::RESULT: {
        if (argc < 4) {
          out.error("Error: missing job id\n\n"
                    "Usage:\n"
                    "  instrument-script-server job result <job_id>");
          return out.emit();
        }

        auto jid = require_int(args.at(3), "job id");
        if (!jid) {
          return out.emit();
        }

        return with_client(out, [&](auto &client) {
          instserver::client::v1::MeasureJobResultRequest req;
          req.set_job_id(*jid);

          auto resp =
              call_rpc(out, [&] { return client.measure_job_result(req); });
          if (!resp) {
            return;
          }

          out.message("Job " + std::to_string(*jid) + " result:");

          auto status = resp->status();
          out.message("  Status: " + std::to_string(status));

          if (resp->results().empty()) {
            out.message("  No results");
            return;
          }

          for (const auto &cmd : resp->results()) {
            out.message("  Command:");
            out.message("    Instrument: " + cmd.instrument_name());
            out.message("    Verb: " + cmd.verb());

            for (const auto &param : cmd.param()) {
              std::string value = "<complex>";

              const auto &v = param.value();

              if (v.has_i()) {
                value = std::to_string(v.i());
              } else if (v.has_d()) {
                value = std::to_string(v.d());
              } else if (v.has_b()) {
                value = v.b() ? "true" : "false";
              } else if (v.has_s()) {
                value = v.s();
              }

              out.message("      " + param.name() + " = " + value);
            }
          }
        });
      }

      case SUB_JOB::UNKNOWN:
      default: {
        out.error("Error: unknown job command: " + std::string(action) +
                  "\n\n"
                  "Valid commands:\n"
                  "  measure <script>\n"
                  "  list\n"
                  "  status <job_id>\n"
                  "  cancel <job_id>\n"
                  "  result <job_id>");
        return out.emit();
      }
      }
    }
    case ISS_CLI_Command::DISCOVER: {
      return with_client(out, [&](auto &client) {
        instserver::client::v1::DiscoverRequest req;
        for (int i = 2; i < argc; ++i) {
          req.add_plugin_paths(std::string(args.at(i)));
        }
        auto resp = call_rpc(out, [&] { return client.discover(req); });
        if (!resp.has_value()) {
          return;
        }
        if (resp.value().plugin_names().empty()) {
          out.message("No plugins discovered");
        } else {
          out.message("Discovered plugins:");
          for (const auto &name : resp.value().plugin_names()) {
            out.message("  " + name);
          }
        }
      });
    }
    case ISS_CLI_Command::BUFFER: {
      if (argc < 3) {
        out.error(
            "Error: missing buffer subcommand\n\n"
            "Usage:\n"
            "  instrument-script-server buffer <list|metadata|read|release>");
        return out.emit();
      }

      std::string_view action = args.at(2);

      switch (parse_sub_buffer(action)) {

      case SUB_BUFFER::LIST: {
        return with_client(out, [&](auto &client) {
          instserver::server::v1::ListDataBuffersRequest req;
          auto resp =
              call_rpc(out, [&] { return client.list_data_buffers(req); });

          if (!resp.has_value()) {
            return;
          }

          if (resp->buffers().empty()) {
            out.message("No active shared memory buffers");
          } else {
            out.message("Active buffers:");
            for (const auto &[key, value] : resp->buffers()) {
              out.message(
                  "  " + key + " (" + std::to_string(value.element_count()) +
                  " elements, type=" + readable_datatype(value.data_type()) +
                  ")");
            }
          }
        });
      }

      case SUB_BUFFER::METADATA: {
        if (argc < 4) {
          out.error("Error: missing buffer id\n\n"
                    "Usage:\n"
                    "  instrument-script-server buffer metadata <buffer_id>");
          return out.emit();
        }

        return with_client(out, [&](auto &client) {
          std::string buffer_id = std::string(args.at(3));

          instserver::server::v1::GetBufferMetadataRequest req;
          req.set_buffer_id(buffer_id);

          auto resp =
              call_rpc(out, [&] { return client.get_buffer_metadata(req); });
          if (!resp.has_value()) {
            return;
          }

          const auto &meta = resp->meta();

          out.message("Buffer Metadata:");
          out.message("  ID: " + buffer_id);
          out.message("  Elements: " + std::to_string(meta.element_count()));
          out.message("  Type: " + readable_datatype(meta.data_type()));
          out.message("  Size: " + std::to_string(meta.byte_size()) + " bytes");
        });
      }

      case SUB_BUFFER::READ: {
        if (argc < 4) {
          out.error("Error: missing buffer id\n\n"
                    "Usage:\n"
                    "  instrument-script-server buffer read <buffer_id>");
          return out.emit();
        }

        return with_client(out, [&](auto &client) {
          std::string buffer_id = std::string(args.at(3));

          instserver::client::v1::GetBufferMetadataRequest req;
          req.set_buffer_id(buffer_id);

          auto resp =
              call_rpc(out, [&] { return client.get_buffer_metadata(req); });
          if (!resp.has_value()) {
            return;
          }

          const auto &meta = resp->meta();

          DataBuffer *buf = data_manager_get_buffer(buffer_id.c_str());
          if (!buf) {
            out.error("Error: buffer not found: " + buffer_id);
            return;
          }

          // (rest unchanged)
        });
      }

      case SUB_BUFFER::RELEASE: {
        if (argc < 4) {
          out.error("Error: missing buffer id\n\n"
                    "Usage:\n"
                    "  instrument-script-server buffer release <buffer_id>");
          return out.emit();
        }

        return with_client(out, [&](auto &client) {
          std::string buffer_id = std::string(args.at(3));

          instserver::client::v1::ReleaseBufferRequest req;
          req.set_buffer_id(buffer_id);

          auto resp = call_rpc(out, [&] { return client.release_buffer(req); });
          if (!resp.has_value()) {
            return;
          }

          out.message("Released buffer: " + buffer_id);
        });
      }

      case SUB_BUFFER::UNKNOWN:
      default: {
        out.error("Error: unknown buffer command: " + std::string(action) +
                  "\n\n"
                  "Valid commands:\n"
                  "  list\n"
                  "  metadata <id>\n"
                  "  read <id>\n"
                  "  release <id>");
        return out.emit();
      }
      }
    }
    case ISS_CLI_Command::HELP_SHORT:
    case ISS_CLI_Command::HELP: {
      print_usage();
      return out.emit();
    }
    case ISS_CLI_Command::VERSION_SHORT:
    case ISS_CLI_Command::VERSION: {
      const std::string version = INSTSERVER_VERSION;
      const std::string tag = INSTSERVER_GIT_TAG;
      const std::string commit = INSTSERVER_GIT_COMMIT;
      std::string version_str = "instrument-script-server " + version;
      if (!tag.empty()) {
        version_str += " (" + tag + ")";
      }
      if (commit != "unknown") {
        version_str += " [" + commit.substr(0, 7) + "]";
      }
      out.message(version_str);
      return out.emit();
    }
    default: {
      out.error("Unknown command: " + std::string(command));
      print_usage();
    }
    }
    return out.emit();
  } catch (const std::exception &e) {
    out.error(e.what());
  }

  return out.emit();
}
