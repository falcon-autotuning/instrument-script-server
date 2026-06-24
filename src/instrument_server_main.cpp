#include "instrument-script-server/instrument-script-server-client.h"
#include "instrument-script-server/version.hpp"
#include <inst_logging.h>
#include <iostream>
#include <spdlog/spdlog.h>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif
#include <nlohmann/json.hpp>

struct CLIOutput {
  bool json_mode = false;

  std::vector<std::string> messages;
  std::vector<std::string> errors;
  std::vector<nlohmann::json> outputs;

  void message(const std::string &msg) { messages.push_back(msg); }

  void error(const std::string &msg) { errors.push_back(msg); }

  void output(const nlohmann::json &obj) { outputs.push_back(obj); }

  template <typename Fn> void output_proto(Fn fn) { outputs.push_back(fn()); }

  int emit() const {
    if (json_mode) {
      nlohmann::json j;

      j["ok"] = errors.empty();

      if (!messages.empty())
        j["message"] = messages;

      if (!errors.empty())
        j["error"] = errors;

      if (!outputs.empty())
        j["output"] = outputs;

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

using namespace instserver;

static volatile bool g_running = true;

namespace {
uint16_t get_port() {
  const char *env = std::getenv("INSTRUMENT_SCRIPT_SERVER_RPC_PORT");
  if (!env)
    return 50051;
  return static_cast<uint16_t>(std::stoi(env));
}

instrument_server_client_t *connect_client() {
  return instrument_server_client_create(get_port());
}

void die(const std::string &msg) {
  std::cerr << msg << "\n";
  std::exit(1);
}
} // namespace
void signal_handler(int sig) {
  (void)sig;
  g_running = false;
}

void print_usage() {
  std::cout << "Usage: instrument-script-server <command> [options]\n\n";
  std::cout << "Daemon Management:\n";
  std::cout << "  daemon start                       Start server daemon\n";
  std::cout << "  daemon stop                        Stop server daemon\n";
  std::cout << "  daemon status                      Check daemon status\n";
  std::cout << "\nInstrument Commands:\n";
  std::cout << "  start <config> [--plugin <path>]   Start instrument\n";
  std::cout << "  stop <name>                        Stop instrument\n";
  std::cout << "  status <name>                      Query instrument status\n";
  std::cout
      << "  list                               List running instruments\n";
  std::cout << "\nMeasurement:\n";
  std::cout << "  measure <script>  [--globals <string>]"
               "[--json]\n";
  std::cout << "                               Run Lua measurement script\n";
  std::cout << "\nUtilities:\n";
  std::cout << "  discover [paths...]                Discover plugins\n";
  std::cout << "\nBuffer Management:\n";
  std::cout << "  list-buffers                       List all active shared "
               "memory buffers\n";
  std::cout << "  buffer-metadata <id>               Show metadata for a "
               "shared memory buffer\n";
  std::cout << "  read-buffer <id> [--json]          Read data contents of a "
               "shared memory buffer\n";
  std::cout << "  release-buffer <id>                Deallocate/free a shared "
               "memory buffer\n";
  std::cout << "\nOptions:\n";
  std::cout << "  --log-level <level>  Log level (default: info)\n";
  std::cout << "  --version, -v        Show version information\n";
  std::cout << "  --help, -h           Show this help message\n";
  std::cout << "\nWorkflow:\n";
  std::cout << "  1. Start daemon:\n";
  std::cout << "     instrument-script-server daemon start\n";
  std::cout << "\n  2. Start instruments:\n";
  std::cout << "     instrument-script-server start dac1.yaml\n";
  std::cout << "     instrument-script-server start dmm1.yaml\n";
  std::cout << "     instrument-script-server start scope1.yaml --plugin "
               "./custom.so\n";
  std::cout << "\n  3. Run measurement:\n";
  std::cout << "     instrument-script-server measure my_measurement.lua\n";
  std::cout << "\n  4. Manage:\n";
  std::cout << "     instrument-script-server list\n";
  std::cout << "     instrument-script-server status DAC1\n";
  std::cout << "     instrument-script-server stop DAC1\n";
  std::cout << "\n  5. Shutdown:\n";
  std::cout << "     instrument-script-server daemon stop\n";
}

namespace {
enum class ISS_CLI_Command : std::uint8_t {
  DAEMON,
  START,
  STOP,
  LIST,
  LIST_BUFFERS,
  BUFFER_METADATA,
  RELEASE_BUFFER,
  READ_BUFFER,
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

constexpr std::array<CommandEntry, 15> command_table{
    {{ISS_CLI_Command::DAEMON, "daemon"},
     {ISS_CLI_Command::START, "start"},
     {ISS_CLI_Command::STOP, "stop"},
     {ISS_CLI_Command::LIST, "list"},
     {ISS_CLI_Command::LIST_BUFFERS, "list-buffers"},
     {ISS_CLI_Command::BUFFER_METADATA, "buffer-metadata"},
     {ISS_CLI_Command::RELEASE_BUFFER, "release-buffer"},
     {ISS_CLI_Command::READ_BUFFER, "read-buffer"},
     {ISS_CLI_Command::MEASURE, "measure"},
     {ISS_CLI_Command::STATUS, "status"},
     {ISS_CLI_Command::HELP, "--help"},
     {ISS_CLI_Command::HELP_SHORT, "-h"},
     {ISS_CLI_Command::VERSION, "--version"},
     {ISS_CLI_Command::VERSION_SHORT, "-v"},
     {ISS_CLI_Command::DISCOVER, "discover"}}};

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
constexpr SUB_DAEMON parse_sub_daemon(const std::string &s) {
  if (s == "start")
    return SUB_DAEMON::START;
  if (s == "stop")
    return SUB_DAEMON::STOP;
  if (s == "status")
    return SUB_DAEMON::STATUS;
  return SUB_DAEMON::UNKNOWN;
}
constexpr uint8_t parse_log_level(const std::string &s) {
  if (s == "trace")
    return INST_LOG_TRACE;
  if (s == "debug")
    return INST_LOG_DEBUG;
  if (s == "info")
    return INST_LOG_INFO;
  if (s == "warn")
    return INST_LOG_WARN;
  if (s == "error")
    return INST_LOG_ERROR;
  throw std::runtime_error("Invalid log level: " + s);
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  std::string command = argv[1];
  int rc;
  CLIOutput out{};
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--json") {
      out.json_mode = true;
      break;
    }
  }
  switch (parse_command(command)) {
  case ISS_CLI_Command::DAEMON: {
    // subcommand is positional 0
    if (argc < 3) {
      out.error("Usage: instrument-script-server daemon <start|stop|status>");
      return out.emit();
    }
    std::string action = argv[2];
    switch (parse_sub_daemon(action)) {
    case SUB_DAEMON::START: {
      std::string log_level;
      for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--log-level" && i + 1 < argc) {
          log_level = argv[i + 1];
          parse_log_level(log_level);
          break;
        }
      }
#ifdef _WIN32
      char exe_path[MAX_PATH];
      GetModuleFileNameA(NULL, exe_path, MAX_PATH);

      std::string cmd = "instrument-script-server-daemon";

      if (!log_level.empty()) {
        cmd += " --log-level " + log_level;
      }

      // IMPORTANT: must be mutable
      std::vector<char> cmd_buf(cmd.begin(), cmd.end());
      cmd_buf.push_back('\0');

      STARTUPINFOA si{};
      PROCESS_INFORMATION pi{};
      si.cb = sizeof(si);

      BOOL ok = CreateProcessA(NULL, cmd_buf.data(), NULL, NULL, FALSE,
                               DETACHED_PROCESS | CREATE_NO_WINDOW, NULL, NULL,
                               &si, &pi);

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

        execlp("instrument-script-server-daemon",
               "instrument-script-server-daemon", "--log-level",
               log_level.c_str(), nullptr);

        _exit(1); // exec failed
      }
#endif
      int detected_pid = -1;
      bool running = false;

      for (int i = 0; i < 20; ++i) {
        auto *client = instrument_server_client_create(get_port());

        if (client) {
          Instserver__Server__V1__DaemonStatusRequest req =
              INSTSERVER__SERVER__V1__DAEMON_STATUS_REQUEST__INIT;

          Instserver__Server__V1__DaemonStatusResponse *resp = nullptr;

          if (instrument_server_client_daemon_status(client, &req, &resp) ==
              0) {
            running = resp->running;
            detected_pid = resp->pid;

            instrument_server_client_free_response(resp);
            instrument_server_client_destroy(client);

            if (running)
              break;
          } else {
            instrument_server_client_destroy(client);
          }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      if (!running) {
        out.error("Daemon failed to start");
        return out.emit();
      }

      out.message("Daemon started");

      out.output({{"pid", detected_pid}, {"running", true}});

      return out.emit();
    }
    case SUB_DAEMON::STOP: {
      auto *client = instrument_server_client_create(get_port());

      if (!client) {
        out.error("Failed to connect to daemon");
        return out.emit();
      }

      Instserver__Server__V1__DaemonStop req =
          INSTSERVER__SERVER__V1__DAEMON_STOP__INIT;

      Instserver__Server__V1__StandardResponse *resp = nullptr;

      int rc = instrument_server_client_stop_daemon(client, &req, &resp);

      if (rc != 0 || resp == nullptr) {
        out.error("Failed to send stop request to daemon");
        instrument_server_client_destroy(client);
        return out.emit();
      }

      if (!resp->ok) {
        out.error("Daemon reported failure while stopping");
        instrument_server_client_free_response(resp);
        instrument_server_client_destroy(client);
        return out.emit();
      }

      out.message("Daemon stopped");

      instrument_server_client_free_response(resp);
      instrument_server_client_destroy(client);

      return out.emit();
    }
    case SUB_DAEMON::STATUS: {
      auto *client = instrument_server_client_create(get_port());

      if (!client) {
        out.error("Failed to connect to daemon");
        return out.emit();
      }

      Instserver__Server__V1__DaemonStatusRequest req =
          INSTSERVER__SERVER__V1__DAEMON_STATUS_REQUEST__INIT;

      Instserver__Server__V1__DaemonStatusResponse *resp = nullptr;

      int rc = instrument_server_client_daemon_status(client, &req, &resp);

      if (rc != 0 || resp == nullptr) {
        out.error("Daemon unreachable");
        instrument_server_client_destroy(client);
        return out.emit();
      }

      if (!resp->standard_response || !resp->standard_response->ok) {
        out.error("Invalid response from daemon");
        instrument_server_client_free_response(resp);
        instrument_server_client_destroy(client);
        return out.emit();
      }

      out.message("Daemon status retrieved");

      out.output_proto([&]() {
        nlohmann::json j;
        j["running"] = resp->running;
        j["pid"] = resp->pid;
        return j;
      });

      instrument_server_client_free_response(resp);
      instrument_server_client_destroy(client);

      return out.emit();
    }
    case SUB_DAEMON::UNKNOWN:
    default:
      out.error("Usage: instrument-script-server daemon <start|stop|status>");
      return out.emit();
    }

  case ISS_CLI_Command::START: {
    if (argc < 2) {
      std::cerr << "Usage: instrument-script-server start <config> [--plugin "
                   "<path>] "
                   "[--log-level <level>]\n";
      rc = 1;
      break;
    }
    std::string config_path = argv[2];
    nlohmann::json params;
    params["config_path"] = config_path;
    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--plugin" && i + 1 < argc) {
        params["plugin"] = argv[++i];
      } else if (arg == "--log-level" && i + 1 < argc) {
        params["log_level"] = argv[++i];
      }
    }

    nlohmann::json out;
    rc = server::handle_start(params, out);
    if (!out.is_null()) {
      if (out.contains("error")) {
        std::cerr << out["error"].get<std::string>() << "\n";
      }
      if (out.contains("instrument")) {
        std::cout << "Started instrument: "
                  << out["instrument"].get<std::string>() << "\n";
      }
    }
    break;
  }
  case ISS_CLI_Command::STOP: {
    if (argc < 3) {
      std::cerr << "Error: stop requires instrument name\n";
      std::cerr << "Usage: instrument-script-server stop <name>\n";
      return 1;
    }
    params["name"] = argv[2];
    rc = server::handle_stop(params, out);
    if (!out.is_null()) {
      if (out.contains("error")) {
        std::cerr << out["error"].get<std::string>() << "\n";
      } else {
        std::cout << "Stopped instrument: " << params["name"].get<std::string>()
                  << "\n";
      }
    }
    return rc;
  }
  case ISS_CLI_Command::STATUS: {
    if (argc < 3) {
      std::cerr << "Error: status requires instrument name\n";
      std::cerr << "Usage: instrument-script-server status <name>\n";
      rc = 1;
      break;
    }
    params["name"] = argv[2];
    rc = server::handle_status(params, out);
    if (!out.is_null()) {
      if (out.contains("error")) {
        std::cerr << out["error"].get<std::string>() << "\n";
      } else {
        std::cout << "Instrument: " << out.value("name", "") << "\n";
        std::cout << "  Status: "
                  << (out.value("alive", false) ? "RUNNING" : "STOPPED")
                  << "\n";
        if (out.contains("stats")) {
          auto s = out["stats"];
          std::cout << "  Commands sent: " << s.value("commands_sent", 0)
                    << "\n";
        }
      }
    }
    break;
  }
  case ISS_CLI_Command::LIST: {
    rc = server::handle_list(params, out);
    if (!out.is_null() && out.contains("instruments")) {
      auto arr = out["instruments"];
      if (arr.empty()) {
        std::cout << "No instruments running\n";
        rc = 1;
        break;
      }
      std::cout << "Running instruments:\n";
      for (auto &name : arr) {
        std::cout << "  " << name.get<std::string>() << "\n";
      }
      rc = 0;
      break;
    }
    break;
  }
  case ISS_CLI_Command::MEASURE: {
    if (argc < 3) {
      std::cerr << "Error: measure requires script path\n";
      std::cerr << "Usage: instrument-script-server measure <script> [--json] ";
      std::cerr << "Usage instrument-script-server measure <script>"
                   "[--globals <string>]"
                   "[--block_inject_globals]"
                   "[--context_schema_version <x.y.z>]"
                   "[--json]"
                   "[--log-level <level>]\n";
      rc = 1;
      break;
    }
    params["script_path"] = argv[2];
    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--log-level" && i + 1 < argc) {
        params["log_level"] = argv[++i];
      } else if (arg == "--json") {
        params["json"] = true;
      } else if (arg == "--globals" && i + 1 < argc) {
        params["globals"] = argv[++i];
      } else if (arg == "--block_inject_globals" && i + 1 < argc) {
        params["block_inject_globals"] = true;
      } else if (arg == "--context_schema_version" && i + 1 < argc) {
        params["context_schema_version"] = argv[++i];
      }
    }
    rc = server::handle_measure(params, out);
    if (!out.is_null()) {
      if (!out.value("ok", false)) {
        std::cerr << out.value("error", "measure failed") << "\n";
      } else if (params.value("json", false)) {
        std::cout << out.dump(2) << "\n";
      } else {
        std::cout << "Measurement complete\n";
      }
    }
    break;
  }
  case ISS_CLI_Command::DISCOVER: {
    if (argc > 2) {
      params["paths"] = nlohmann::json::array();
      for (int i = 2; i < argc; ++i) {
        params["paths"].push_back(argv[i]);
      }
    }
    rc = server::handle_discover(params, out);
    if (!out.is_null()) {
      if (out.contains("protocols")) {
        auto p = out["protocols"];
        std::cout << "Found " << p.size() << " plugin(s):\n";
        for (auto &proto : p) {
          std::cout << "  " << proto.get<std::string>() << "\n";
        }
      }
    }
    break;
  }
  case ISS_CLI_Command::LIST_BUFFERS: {
    if (command == "list-buffers") {
      nlohmann::json params;
      nlohmann::json out;
      int rc = server::handle_list_buffers(params, out);
      if (!out.is_null()) {
        if (!out.value("ok", false)) {
          std::cerr << out.value("error", "Failed to list buffers") << "\n";
        } else if (out.contains("buffers")) {
          auto arr = out["buffers"];
          if (arr.empty()) {
            std::cout << "No active shared memory buffers\n";
          } else {
            std::cout << "Active Shared Memory Buffers:\n";
            for (auto &buf_id : arr) {
              nlohmann::json meta_params;
              nlohmann::json meta_out;
              meta_params["buffer_id"] = buf_id.get<std::string>();
              if (server::handle_get_buffer_metadata(meta_params, meta_out) ==
                      0 &&
                  meta_out.value("ok", false)) {
                std::cout << "  - " << buf_id.get<std::string>() << " ("
                          << meta_out.value("element_count", 0ULL)
                          << " elements, " << meta_out.value("data_type", "")
                          << ")\n";
              } else {
                std::cout << "  - " << buf_id.get<std::string>() << "\n";
              }
            }
          }
        }
      }
      break;
    }
  }
  case ISS_CLI_Command::BUFFER_METADATA: {
    if (argc < 3) {
      std::cerr << "Error: buffer-metadata requires buffer ID\n";
      std::cerr
          << "Usage: instrument-script-server buffer-metadata <buffer_id>\n";
      rc = 1;
      break;
    }
    nlohmann::json params;
    params["buffer_id"] = argv[2];
    nlohmann::json out;
    int rc = server::handle_get_buffer_metadata(params, out);
    if (!out.is_null()) {
      if (!out.value("ok", false)) {
        std::cerr << out.value("error", "Failed to get buffer metadata")
                  << "\n";
      } else {
        std::cout << "Buffer Metadata:\n";
        std::cout << "  ID: " << argv[2] << "\n";
        std::cout << "  Elements: " << out.value("element_count", 0ULL) << "\n";
        std::cout << "  Type: " << out.value("data_type", "") << "\n";
        std::cout << "  Size: " << out.value("bytes", 0ULL) << " bytes\n";
      }
    }
    break;
  }
  case ISS_CLI_Command::READ_BUFFER: {
    if (argc < 3) {
      std::cerr << "Error: read-buffer requires buffer ID\n";
      std::cerr << "Usage: instrument-script-server read-buffer <buffer_id> "
                   "[--json]\n";
      rc = 1;
      break;
    }
    params["buffer_id"] = argv[2];
    bool json_output = false;
    for (int i = 3; i < argc; ++i) {
      if (std::string(argv[i]) == "--json") {
        json_output = true;
      }
    }
    rc = server::handle_read_buffer(params, out);
    if (!out.is_null()) {
      if (!out.value("ok", false)) {
        std::cerr << out.value("error", "Failed to read buffer") << "\n";
      } else if (json_output) {
        std::cout << out.dump(2) << "\n";
      } else if (out.contains("data")) {
        auto data = out["data"];
        for (size_t i = 0; i < data.size(); ++i) {
          std::cout << "[" << i << "] " << data[i] << "\n";
        }
      }
    }
    break;
  }
  case ISS_CLI_Command::RELEASE_BUFFER: {
    if (argc < 3) {
      std::cerr << "Error: release-buffer requires buffer ID\n";
      std::cerr
          << "Usage: instrument-script-server release-buffer <buffer_id>\n";
      rc = 1;
      break;
    }
    params["buffer_id"] = argv[2];
    rc = server::handle_release_buffer(params, out);
    if (!out.is_null()) {
      if (!out.value("ok", false)) {
        std::cerr << out.value("error", "Failed to release buffer") << "\n";
      } else {
        std::cout << "Released buffer: " << argv[2] << "\n";
      }
    }
    break;
  }
  case ISS_CLI_Command::HELP_SHORT:
  case ISS_CLI_Command::HELP: {
    print_usage();
    rc = 0;
    break;
  }
  case ISS_CLI_Command::VERSION_SHORT:
  case ISS_CLI_Command::VERSION: {
    std::cout << "instrument-script-server " << get_full_version() << '\n';
    rc = 0;
    break;
  }
  default: {
    std::cerr << "Unknown command: " << command << "\n\n";
    print_usage();
    rc = 1;
  }
    return rc;
  }
  }
  int handle_read_buffer(const json &params, json &out) {
    out = json::object();

    std::string buffer_id = params.value("buffer_id", "");
    if (buffer_id.empty()) {
      out["ok"] = false;
      out["error"] = "missing buffer_id";
      return 1;
    }

    DataBuffer *buf = data_manager_get_buffer(buffer_id.c_str());
    if (buf == nullptr) {
      out["ok"] = false;
      out["error"] = "buffer not found: " + buffer_id;
      return 1;
    }

    void *data = data_buffer_data(buf);
    size_t n = data_buffer_element_count(buf);

    out["ok"] = true;
    out["buffer_id"] = buffer_id;
    out["element_count"] = n;

    // get metadata (since as_floatXX is gone)
    auto meta_opt = ipc::DataBufferManager::instance().get_metadata(buffer_id);
    if (!meta_opt) {
      out["ok"] = false;
      out["error"] = "metadata not found";
      return 1;
    }

    const auto &meta = *meta_opt;

    if (meta.data_type() == INST_DATA_FLOAT64) {
      out["data"] = make_vector<double>(data, n);
      out["data_type"] = INST_DATA_FLOAT64;

    } else if (meta.data_type() == INST_DATA_FLOAT32) {
      auto fvec = make_vector<float>(data, n);

      std::vector<double> converted(fvec.begin(), fvec.end());
      out["data"] = std::move(converted);
      out["data_type"] = INST_DATA_FLOAT64;

    } else {
      out["ok"] = false;
      out["error"] = "unsupported buffer data type for JSON export";
      return 1;
    }

    return 0;
  }
