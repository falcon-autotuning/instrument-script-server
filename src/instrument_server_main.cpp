#include "instrument-script-server/instrument-script-server-client.h"
#include "instrument-script-server/ipc/DataBufferManager.hpp"
#include "instrument-script-server/server/CommandHandlers.hpp"
#include "instrument-script-server/version.hpp"
#include <filesystem>
#include <inst_logging.h>
#include <instrument-data.h>
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
using namespace instserver::server;
using namespace instserver::server::v1;

static volatile bool g_running = true;

namespace {
uint16_t get_port() {
  const char *env = std::getenv("INSTRUMENT_SCRIPT_SERVER_RPC_PORT");
  if (!env)
    return 8555;
  return static_cast<uint16_t>(std::stoi(env));
}

instrument_server_client_t *connect_client() {
  return instrument_server_client_create(get_port());
}

void die(const std::string &msg) {
  std::cerr << msg << "\n";
  std::exit(1);
}
template <typename T> std::vector<T> make_vector(const void *data, size_t n) {
  const T *ptr = static_cast<const T *>(data);
  return std::vector<T>(ptr, ptr + n);
}

// ReadBuffer is handled in the CLI process (data is read directly from shared
// memory, not via RPC). Buffer metadata is fetched via handle_get_buffer_metadata.
static int local_read_buffer(const std::string &buffer_id,
                             nlohmann::json &out) {
  out = nlohmann::json::object();

  GetBufferMetadataRequest meta_req;
  meta_req.set_buffer_id(buffer_id);
  GetBufferMetadataResponse meta_resp;
  if (handle_get_buffer_metadata(meta_req, &meta_resp) != 0 ||
      !meta_resp.standard_response().ok()) {
    out["ok"] = false;
    out["error"] = "buffer not found: " + buffer_id;
    return 1;
  }

  const auto &meta = meta_resp.meta();
  uint32_t element_count = meta.element_count();
  uint32_t data_type = meta.data_type();

  // Get raw data from shared memory via C API (instrument-data.h)
  DataBuffer *buf = data_manager_get_buffer(buffer_id.c_str());
  if (!buf) {
    out["ok"] = false;
    out["error"] = "buffer data not found: " + buffer_id;
    return 1;
  }

  void *data = data_buffer_data(buf);
  size_t n = static_cast<size_t>(element_count);

  out["ok"] = true;
  out["buffer_id"] = buffer_id;
  out["element_count"] = n;

  if (data_type == INST_DATA_FLOAT64) {
    out["data"] = make_vector<double>(data, n);
    out["data_type"] = "float64";
  } else if (data_type == INST_DATA_FLOAT32) {
    auto fvec = make_vector<float>(data, n);
    out["data"] = std::vector<double>(fvec.begin(), fvec.end());
    out["data_type"] = "float32";
  } else {
    out["ok"] = false;
    out["error"] = "unsupported buffer data type for export";
    return 1;
  }
  return 0;
}
} // namespace
void signal_handler(int sig) {
  (void)sig;
  g_running = false;
}

// Returns the path to instrument-script-server-daemon, preferring co-location
// with this binary (argv[0]), then falling back to the bare name (PATH lookup).
static std::string get_daemon_path(const char *argv0) {
  if (argv0) {
    std::filesystem::path self(argv0);
    auto sibling = self.parent_path() / "instrument-script-server-daemon";
    if (std::filesystem::exists(sibling)) {
      return sibling.string();
    }
  }
  return "instrument-script-server-daemon";
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
  int rc = 0;
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

        std::string daemon_path = get_daemon_path(argv[0]);
        execl(daemon_path.c_str(), daemon_path.c_str(), "--log-level",
              log_level.empty() ? "info" : log_level.c_str(), nullptr);

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

      out.message("daemon started");

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

      int stop_rc = instrument_server_client_stop_daemon(client, &req, &resp);

      if (stop_rc != 0 || resp == nullptr) {
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

      int status_rc = instrument_server_client_daemon_status(client, &req, &resp);

      if (status_rc != 0 || resp == nullptr) {
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
  } // end case ISS_CLI_Command::DAEMON

  case ISS_CLI_Command::START: {
    if (argc < 3) {
      std::cerr << "Usage: instrument-script-server start <config> [--plugin "
                   "<path>] "
                   "[--log-level <level>]\n";
      rc = 1;
      break;
    }
    auto *client = connect_client();
    bool use_rpc = false;
    if (client) {
      Instserver__Server__V1__DaemonStatusRequest sreq =
          INSTSERVER__SERVER__V1__DAEMON_STATUS_REQUEST__INIT;
      Instserver__Server__V1__DaemonStatusResponse *sresp = nullptr;
      if (instrument_server_client_daemon_status(client, &sreq, &sresp) == 0 && sresp && sresp->running) {
        use_rpc = true;
      }
      if (sresp) instrument_server_client_free_response(sresp);
    }

    if (use_rpc) {
      Instserver__Server__V1__StartInstrumentRequest req =
          INSTSERVER__SERVER__V1__START_INSTRUMENT_REQUEST__INIT;
      req.config_path = argv[2];
      std::string log_level = "info";
      for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--plugin" && i + 1 < argc) {
          req.plugin_path = argv[++i];
        } else if (arg == "--log-level" && i + 1 < argc) {
          log_level = argv[++i];
        }
      }
      req.log_level = const_cast<char*>(log_level.c_str());
      Instserver__Server__V1__StartInstrumentResponse *resp = nullptr;
      int call_rc = instrument_server_client_start_instrument(client, &req, &resp);
      if (call_rc != 0 || resp == nullptr) {
        std::cerr << "Failed to send start instrument request to daemon\n";
        rc = 1;
      } else if (!resp->standard_response || !resp->standard_response->ok) {
        std::cerr << (resp->standard_response && resp->standard_response->error ?
                      resp->standard_response->error->message : "Unknown error starting instrument") << "\n";
        rc = 1;
      } else {
        std::cout << "Instrument started successfully\n";
        rc = 0;
      }
      if (resp) instrument_server_client_free_response(resp);
    } else {
      StartInstrumentRequest req;
      req.set_config_path(argv[2]);
      std::string log_level = "info";
      for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--plugin" && i + 1 < argc) {
          req.set_plugin_path(argv[++i]);
        } else if (arg == "--log-level" && i + 1 < argc) {
          log_level = argv[++i];
        }
      }
      req.set_log_level(log_level);
      StartInstrumentResponse resp;
      rc = handle_start_instrument(req, &resp);
      if (!resp.standard_response().ok()) {
        std::cerr << resp.standard_response().error().message() << "\n";
        rc = 1;
      } else {
        std::cout << "Instrument started successfully\n";
      }
    }
    if (client) instrument_server_client_destroy(client);
    break;
  }
  case ISS_CLI_Command::STOP: {
    if (argc < 3) {
      std::cerr << "Error: stop requires instrument name\n";
      std::cerr << "Usage: instrument-script-server stop <name>\n";
      return 1;
    }
    auto *client = connect_client();
    bool use_rpc = false;
    if (client) {
      Instserver__Server__V1__DaemonStatusRequest sreq =
          INSTSERVER__SERVER__V1__DAEMON_STATUS_REQUEST__INIT;
      Instserver__Server__V1__DaemonStatusResponse *sresp = nullptr;
      if (instrument_server_client_daemon_status(client, &sreq, &sresp) == 0 && sresp && sresp->running) {
        use_rpc = true;
      }
      if (sresp) instrument_server_client_free_response(sresp);
    }

    if (use_rpc) {
      Instserver__Server__V1__StopInstrumentRequest req =
          INSTSERVER__SERVER__V1__STOP_INSTRUMENT_REQUEST__INIT;
      req.instrument_name = argv[2];
      Instserver__Server__V1__StopInstrumentResponse *resp = nullptr;
      int call_rc = instrument_server_client_stop_instrument(client, &req, &resp);
      if (call_rc != 0 || resp == nullptr) {
        std::cerr << "Failed to send stop request to daemon\n";
        rc = 1;
      } else if (!resp->standard_response || !resp->standard_response->ok) {
        std::cerr << (resp->standard_response && resp->standard_response->error ?
                      resp->standard_response->error->message : "Unknown error stopping instrument") << "\n";
        rc = 1;
      } else {
        std::cout << "Stopped instrument: " << argv[2] << "\n";
        rc = 0;
      }
      if (resp) instrument_server_client_free_response(resp);
    } else {
      StopInstrumentRequest req;
      req.set_instrument_name(argv[2]);
      StopInstrumentResponse resp;
      rc = handle_stop_instrument(req, &resp);
      if (!resp.standard_response().ok()) {
        std::cerr << resp.standard_response().error().message() << "\n";
        rc = 1;
      } else {
        std::cout << "Stopped instrument: " << argv[2] << "\n";
      }
    }
    if (client) instrument_server_client_destroy(client);
    break;
  }
  case ISS_CLI_Command::STATUS: {
    if (argc < 3) {
      std::cerr << "Error: status requires instrument name\n";
      std::cerr << "Usage: instrument-script-server status <name>\n";
      rc = 1;
      break;
    }
    auto *client = connect_client();
    bool use_rpc = false;
    if (client) {
      Instserver__Server__V1__DaemonStatusRequest sreq =
          INSTSERVER__SERVER__V1__DAEMON_STATUS_REQUEST__INIT;
      Instserver__Server__V1__DaemonStatusResponse *sresp = nullptr;
      if (instrument_server_client_daemon_status(client, &sreq, &sresp) == 0 && sresp && sresp->running) {
        use_rpc = true;
      }
      if (sresp) instrument_server_client_free_response(sresp);
    }

    if (use_rpc) {
      Instserver__Server__V1__InstrumentStatusRequest req =
          INSTSERVER__SERVER__V1__INSTRUMENT_STATUS_REQUEST__INIT;
      req.instrument_name = argv[2];
      Instserver__Server__V1__InstrumentStatusResponse *resp = nullptr;
      int call_rc = instrument_server_client_instrument_status(client, &req, &resp);
      if (call_rc != 0 || resp == nullptr) {
        std::cerr << "Failed to get instrument status from daemon\n";
        rc = 1;
      } else if (!resp->standard_response || !resp->standard_response->ok) {
        std::cerr << (resp->standard_response && resp->standard_response->error ?
                      resp->standard_response->error->message : "Unknown error getting status") << "\n";
        rc = 1;
      } else {
        std::cout << "Instrument: " << argv[2] << "\n";
        std::cout << "  Status: RUNNING\n";
        if (resp->stats) {
          std::cout << "  Commands sent: " << resp->stats->commands_sent << "\n";
        }
        rc = 0;
      }
      if (resp) instrument_server_client_free_response(resp);
    } else {
      InstrumentStatusRequest req;
      req.set_instrument_name(argv[2]);
      InstrumentStatusResponse resp;
      rc = handle_instrument_status(req, &resp);
      if (!resp.standard_response().ok()) {
        std::cerr << resp.standard_response().error().message() << "\n";
        rc = 1;
      } else {
        std::cout << "Instrument: " << argv[2] << "\n";
        std::cout << "  Status: RUNNING\n";
        if (resp.has_stats()) {
          std::cout << "  Commands sent: " << resp.stats().commands_sent() << "\n";
        }
      }
    }
    if (client) instrument_server_client_destroy(client);
    break;
  }
  case ISS_CLI_Command::LIST: {
    auto *client = connect_client();
    bool use_rpc = false;
    if (client) {
      Instserver__Server__V1__DaemonStatusRequest sreq =
          INSTSERVER__SERVER__V1__DAEMON_STATUS_REQUEST__INIT;
      Instserver__Server__V1__DaemonStatusResponse *sresp = nullptr;
      if (instrument_server_client_daemon_status(client, &sreq, &sresp) == 0 && sresp && sresp->running) {
        use_rpc = true;
      }
      if (sresp) instrument_server_client_free_response(sresp);
    }

    if (use_rpc) {
      Instserver__Server__V1__ListInstrumentsRequest req =
          INSTSERVER__SERVER__V1__LIST_INSTRUMENTS_REQUEST__INIT;
      Instserver__Server__V1__ListInstrumentsResponse *resp = nullptr;
      int call_rc = instrument_server_client_list_instruments(client, &req, &resp);
      if (call_rc != 0 || resp == nullptr) {
        std::cerr << "Failed to list instruments from daemon\n";
        rc = 1;
      } else if (!resp->standard_response || !resp->standard_response->ok) {
        std::cerr << (resp->standard_response && resp->standard_response->error ?
                      resp->standard_response->error->message : "Unknown error listing instruments") << "\n";
        rc = 1;
      } else {
        if (resp->n_instrument_name == 0) {
          std::cout << "No instruments running\n";
          rc = 1;
        } else {
          std::cout << "Running instruments:\n";
          for (size_t i = 0; i < resp->n_instrument_name; ++i) {
            std::cout << "  " << resp->instrument_name[i] << "\n";
          }
          rc = 0;
        }
      }
      if (resp) instrument_server_client_free_response(resp);
    } else {
      ListInstrumentsRequest req;
      ListInstrumentsResponse resp;
      rc = handle_list_instruments(req, &resp);
      if (!resp.standard_response().ok()) {
        std::cerr << resp.standard_response().error().message() << "\n";
        rc = 1;
        break;
      }
      if (resp.instrument_name().empty()) {
        std::cout << "No instruments running\n";
        rc = 1;
        break;
      }
      std::cout << "Running instruments:\n";
      for (const auto &name : resp.instrument_name()) {
        std::cout << "  " << name << "\n";
      }
      rc = 0;
    }
    if (client) instrument_server_client_destroy(client);
    break;
  }
  case ISS_CLI_Command::MEASURE: {
    if (argc < 3) {
      std::cerr << "Error: measure requires script path\n";
      std::cerr << "Usage: instrument-script-server measure <script> [--json] ";
      std::cerr << "       [--globals <json_string>] [--json] [--log-level <level>]\n";
      rc = 1;
      break;
    }
    MeasureJobRequest req;
    req.set_script_path(argv[2]);
    bool json_output = false;
    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--json") {
        json_output = true;
      }
      // Note: --globals, --block_inject_globals, --context_schema_version
      // require additional protobuf fields; skipped for now
    }
    MeasureJobResultResponse resp;
    rc = handle_measure(req, &resp);
    if (!resp.standard_response().ok()) {
      std::cerr << resp.standard_response().error().message() << "\n";
      rc = 1;
    } else if (json_output) {
      // Serialize results to JSON
      nlohmann::json j;
      j["ok"] = true;
      j["status"] = resp.status();
      nlohmann::json results = nlohmann::json::array();
      for (const auto &res : resp.results()) {
        nlohmann::json r;
        r["instrument"] = res.instrument_name();
        r["verb"] = res.verb();
        results.push_back(r);
      }
      j["results"] = results;
      std::cout << j.dump(2) << "\n";
    } else {
      std::cout << "Measurement complete\n";
    }
    break;
  }
  case ISS_CLI_Command::DISCOVER: {
    auto *client = connect_client();
    bool use_rpc = false;
    if (client) {
      Instserver__Server__V1__DaemonStatusRequest sreq =
          INSTSERVER__SERVER__V1__DAEMON_STATUS_REQUEST__INIT;
      Instserver__Server__V1__DaemonStatusResponse *sresp = nullptr;
      if (instrument_server_client_daemon_status(client, &sreq, &sresp) == 0 && sresp && sresp->running) {
        use_rpc = true;
      }
      if (sresp) instrument_server_client_free_response(sresp);
    }

    if (use_rpc) {
      Instserver__Server__V1__DiscoverRequest req =
          INSTSERVER__SERVER__V1__DISCOVER_REQUEST__INIT;
      std::vector<char*> paths;
      for (int i = 2; i < argc; ++i) {
        paths.push_back(argv[i]);
      }
      req.n_plugin_paths = paths.size();
      req.plugin_paths = paths.data();

      Instserver__Server__V1__DiscoverResponse *resp = nullptr;
      int call_rc = instrument_server_client_discover(client, &req, &resp);
      if (call_rc != 0 || resp == nullptr) {
        std::cerr << "Failed to discover plugins from daemon\n";
        rc = 1;
      } else if (!resp->standard_response || !resp->standard_response->ok) {
        std::cerr << (resp->standard_response && resp->standard_response->error ?
                      resp->standard_response->error->message : "Unknown error discovering plugins") << "\n";
        rc = 1;
      } else {
        if (resp->n_plugin_names == 0) {
          std::cout << "No plugins discovered\n";
        } else {
          std::cout << "Discovered plugins:\n";
          for (size_t i = 0; i < resp->n_plugin_names; ++i) {
            std::cout << "  " << resp->plugin_names[i] << "\n";
          }
        }
        rc = 0;
      }
      if (resp) instrument_server_client_free_response(resp);
    } else {
      DiscoverRequest req;
      for (int i = 2; i < argc; ++i) {
        req.add_plugin_paths(argv[i]);
      }
      DiscoverResponse resp;
      rc = handle_discover(req, &resp);
      if (!resp.standard_response().ok()) {
        std::cerr << resp.standard_response().error().message() << "\n";
        rc = 1;
      } else {
        std::cout << "Found " << resp.paths().size() << " plugin(s):\n";
        for (const auto &path : resp.paths()) {
          std::cout << "  " << path << "\n";
        }
      }
    }
    if (client) instrument_server_client_destroy(client);
    break;
  }
  case ISS_CLI_Command::LIST_BUFFERS: {
    ListDataBuffersRequest req;
    ListDataBuffersResponse resp;
    rc = handle_list_buffers(req, &resp);
    if (!resp.standard_response().ok()) {
      std::cerr << resp.standard_response().error().message() << "\n";
      rc = 1;
    } else if (resp.buffers().empty()) {
      std::cout << "No active shared memory buffers\n";
    } else {
      std::cout << "Active Shared Memory Buffers:\n";
      for (const auto &kv : resp.buffers()) {
        const auto &meta = kv.second;
        std::cout << "  - " << kv.first
                  << " (" << meta.element_count() << " elements, type="
                  << meta.data_type() << ")\n";
      }
    }
    break;
  }
  case ISS_CLI_Command::BUFFER_METADATA: {
    if (argc < 3) {
      std::cerr << "Error: buffer-metadata requires buffer ID\n";
      std::cerr
          << "Usage: instrument-script-server buffer-metadata <buffer_id>\n";
      rc = 1;
      break;
    }
    GetBufferMetadataRequest req;
    req.set_buffer_id(argv[2]);
    GetBufferMetadataResponse resp;
    rc = handle_get_buffer_metadata(req, &resp);
    if (!resp.standard_response().ok()) {
      std::cerr << resp.standard_response().error().message() << "\n";
      rc = 1;
    } else {
      const auto &meta = resp.meta();
      std::cout << "Buffer Metadata:\n";
      std::cout << "  ID: " << argv[2] << "\n";
      std::cout << "  Elements: " << meta.element_count() << "\n";
      std::cout << "  Type: " << meta.data_type() << "\n";
      std::cout << "  Size: " << meta.byte_size() << " bytes\n";
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
    bool json_output_rb = false;
    for (int i = 3; i < argc; ++i) {
      if (std::string(argv[i]) == "--json") {
        json_output_rb = true;
      }
    }
    nlohmann::json rb_out;
    rc = local_read_buffer(argv[2], rb_out);
    if (!rb_out.value("ok", false)) {
      std::cerr << rb_out.value("error", "Failed to read buffer") << "\n";
    } else if (json_output_rb) {
      std::cout << rb_out.dump(2) << "\n";
    } else if (rb_out.contains("data")) {
      auto data = rb_out["data"];
      for (size_t i = 0; i < data.size(); ++i) {
        std::cout << "[" << i << "] " << data[i] << "\n";
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
    ReleaseBufferRequest req;
    req.set_buffer_id(argv[2]);
    ReleaseBufferResponse resp;
    rc = handle_release_buffer(req, &resp);
    if (!resp.standard_response().ok()) {
      std::cerr << resp.standard_response().error().message() << "\n";
      rc = 1;
    } else {
      std::cout << "Released buffer: " << argv[2] << "\n";
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
  }
  return rc;
}
