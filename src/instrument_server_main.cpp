#include "instrument-script-server/client/instrument-server-client.hpp"
#include "instserver/server/v1/daemon_messages.pb.h"
#include <filesystem>
#include <google/protobuf/util/json_util.h>
#include <instrument-data.h>
#include <instrument-log/inst_logging.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
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
    google::protobuf::util::MessageToJsonString(msg, &json);
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
  int argc;
  char **argv;

  // start index (skip program + command args if desired)
  int start = 1;

  CliArgs(int argc_, char **argv_, int start_ = 1)
      : argc(argc_), argv(argv_), start(start_) {}

  // check if flag exists
  [[nodiscard]] bool has_flag(const std::string &name) const {
    std::string key = "--" + name;

    for (int i = start; i < argc; ++i) {
      if (argv[i] == key) {
        return true;
      }
    }
    return false;
  }

  // get value after --flag
  [[nodiscard]] std::string
  get_option(const std::string &name, bool require_value = true,
             const std::string &default_value = "") const {
    std::string key = "--" + name;

    for (int i = start; i < argc; ++i) {
      if (argv[i] == key) {
        if (i + 1 < argc) {
          return argv[i + 1];
        }

        return require_value ? "" : default_value;
      }
    }

    return default_value;
  }
};
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
    {{.cmd = ISS_CLI_Command::DAEMON, .name = "daemon"},
     {.cmd = ISS_CLI_Command::START, .name = "start"},
     {.cmd = ISS_CLI_Command::STOP, .name = "stop"},
     {.cmd = ISS_CLI_Command::LIST, .name = "list"},
     {.cmd = ISS_CLI_Command::LIST_BUFFERS, .name = "list-buffers"},
     {.cmd = ISS_CLI_Command::BUFFER_METADATA, .name = "buffer-metadata"},
     {.cmd = ISS_CLI_Command::RELEASE_BUFFER, .name = "release-buffer"},
     {.cmd = ISS_CLI_Command::READ_BUFFER, .name = "read-buffer"},
     {.cmd = ISS_CLI_Command::MEASURE, .name = "measure"},
     {.cmd = ISS_CLI_Command::STATUS, .name = "status"},
     {.cmd = ISS_CLI_Command::HELP, .name = "--help"},
     {.cmd = ISS_CLI_Command::HELP_SHORT, .name = "-h"},
     {.cmd = ISS_CLI_Command::VERSION, .name = "--version"},
     {.cmd = ISS_CLI_Command::VERSION_SHORT, .name = "-v"},
     {.cmd = ISS_CLI_Command::DISCOVER, .name = "discover"}}};

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
  if (s == "start") {
    return SUB_DAEMON::START;
  }
  if (s == "stop") {
    return SUB_DAEMON::STOP;
  }
  if (s == "status") {
    return SUB_DAEMON::STATUS;
  }
  return SUB_DAEMON::UNKNOWN;
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
} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  std::string command = argv[1];
  CLIOutput out{};
  CliArgs args(argc, argv, 3); // skip first 3 arguments
  out.json_mode = args.has_flag("json");
  try {
    switch (parse_command(command)) {
    case ISS_CLI_Command::DAEMON: {
      // subcommand is positional 0
      if (argc < 3) {
        out.error("Usage: instrument-script-server daemon <start|stop|status> "
                  "[--json]");
        return out.emit();
      }
      std::string action = argv[2];
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
        instserver::client::v1::DaemonStatusResponse resp;

        for (int i = 0; i < 20; ++i) {
          try {
            instserver::client::InstrumentServerClient client(get_port());
            instserver::server::v1::DaemonStatusRequest req;
            auto resp = client.daemon_status(req);
            if (resp.running()) {
              running = true;
              break;
            }
          } catch (...) {
            // still starting → ignore
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        out.output_proto_message(resp);

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
          if (resp.has_error()) {
            out.error(resp.error().message());
          } else {
            out.error("Failed to stop daemon");
          }
          return;
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

    case ISS_CLI_Command::START: {
      if (argc < 3) {
        out.error("Usage: start <config> [--json]");
        return out.emit();
      }
      return with_client(out, [&](auto &client) {
        instserver::client::v1::StartInstrumentRequest req;
        req.set_config_path(argv[2]);
        req.set_plugin_path(args.get_option("plugin"));
        req.set_log_level(args.get_option("log-level", true, "info"));
        auto resp = call_rpc(out, [&] { return client.start_instrument(req); });
        if (!resp.has_value()) {
          return;
        }
        out.message("Instrument started successfully");
      });
    }
    case ISS_CLI_Command::STOP: {
      if (argc < 3) {
        out.error("Error: stop requires instrument name\n"
                  "Usage: instrument-script-server stop <name> [--json]");
        return out.emit();
      }
      return with_client(out, [&](auto &client) {
        instserver::client::v1::StopInstrumentRequest req;
        req.set_instrument_name(argv[2]);
        auto resp = call_rpc(out, [&] { return client.stop_instrument(req); });
        if (!resp.has_value()) {
          return;
        }
        out.message("Stopped instrument: " + std::string(argv[2]));
      });
    }
    case ISS_CLI_Command::STATUS: {
      if (argc < 3) {
        out.error("Usage: status <name> [--json]");
        return out.emit();
      }
      return with_client(out, [&](auto &client) {
        instserver::client::v1::InstrumentStatusRequest req;
        req.set_instrument_name(argv[2]);
        auto resp =
            call_rpc(out, [&] { return client.instrument_status(req); });
        if (!resp.has_value()) {
          return;
        }
        out.message("Instrument: " + std::string(argv[2]));
        if (resp.value().has_stats()) {
          out.message("Commands sent: " +
                      std::to_string(resp.value().stats().commands_sent()));
        }
      });
    }
    case ISS_CLI_Command::LIST: {
      return with_client(out, [&](auto &client) {
        instserver::client::v1::ListInstrumentsRequest req;
        auto resp = call_rpc(out, [&] { return client.list_instruments(req); });
        if (!resp.has_value()) {
          return;
        }
        if (resp.value().instrument_name().empty()) {
          out.message("No instruments running");
        } else {
          out.message("Running instruments:");
          for (const auto &name : resp.value().instrument_name()) {
            out.message("  " + name);
          }
        }
      });
    }
    case ISS_CLI_Command::MEASURE: {
      if (argc < 3) {
        out.error("Error: measure requires script path\n"
                  "Usage: instrument-script-server measure <script> [--json]");
        return out.emit();
      }
      return with_client(out, [&](auto &client) {
        instserver::client::v1::MeasureJobRequest req;
        req.set_script_path(argv[2]);
        auto resp = client.measure_job(req);
        if (!resp.standard_response().ok()) {
          out.error(resp.standard_response().error().message());
          out.output_proto_message(resp);
          return;
        }
        uint32_t job_id = resp.job_id();
        while (true) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          instserver::client::v1::JobStatusRequest status_req;
          status_req.set_job_id(job_id);
          auto status_resp = client.job_status(status_req);
          if (!status_resp.has_job()) {
            out.error("Invalid job status response");
            out.output_proto_message(resp);
            return;
          }
          auto status = status_resp.job().status();
          if (status == instserver::client::v1::JobStatus::JOB_STATUS_FAILED ||
              status ==
                  instserver::client::v1::JobStatus::JOB_STATUS_CANCELLED) {
            out.error("Job failed or was cancelled");
            out.output_proto_message(resp);
            return;
          }
          if (status ==
              instserver::client::v1::JobStatus::JOB_STATUS_COMPLETED) {
            instserver::client::v1::MeasureJobResultRequest result_req;
            result_req.set_job_id(job_id);
            out.output_proto_message(resp);
            auto result_resp = client.measure_job_result(result_req);
            if (!result_resp.standard_response().ok()) {
              out.error(result_resp.standard_response().error().message());
              return;
            }
            out.message("Measurement complete");
            return;
          }
        }
      });
    }
    case ISS_CLI_Command::DISCOVER: {
      return with_client(out, [&](auto &client) {
        instserver::client::v1::DiscoverRequest req;
        for (int i = 2; i < argc; ++i) {
          req.add_plugin_paths(argv[i]);
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
    case ISS_CLI_Command::LIST_BUFFERS: {
      return with_client(out, [&](auto &client) {
        instserver::server::v1::ListDataBuffersRequest req;
        auto resp =
            call_rpc(out, [&] { return client.list_data_buffers(req); });
        if (!resp.has_value()) {
          return;
        }
        if (resp.value().buffers().empty()) {
          out.message("No active shared memory buffers");
        } else {
          out.message("Active Shared Memory Buffers:");
          for (const auto &[key, value] : resp.value().buffers()) {
            out.message(
                "  - " + key + " (" + std::to_string(value.element_count()) +
                " elements, type=" + std::to_string(value.data_type()) + ")");
          }
        }
      });
    }
    case ISS_CLI_Command::BUFFER_METADATA: {
      if (argc < 3) {
        out.error("Error: buffer-metadata requires buffer ID\n"
                  "Usage: instrument-script-server buffer-metadata <buffer_id> "
                  "[--json]");
        return out.emit();
      }
      return with_client(out, [&](auto &client) {
        instserver::server::v1::GetBufferMetadataRequest req;
        req.set_buffer_id(argv[2]);
        auto resp =
            call_rpc(out, [&] { return client.get_buffer_metadata(req); });
        if (!resp.has_value()) {
          return;
        }
        const auto &meta = resp.value().meta();
        out.message("Buffer Metadata:");
        out.message("  ID: " + std::string(argv[2]));
        out.message("  Elements: " + std::to_string(meta.element_count()));
        out.message("  Type: " + readable_datatype(meta.data_type()));
        out.message("  Size: " + std::to_string(meta.byte_size()) + " bytes");
      });
    }
    case ISS_CLI_Command::READ_BUFFER: {
      if (argc < 3) {
        out.error(
            "Error: read-buffer requires buffer ID\n"
            "Usage: instrument-script-server read-buffer <buffer_id> [--json]");
        return out.emit();
      }
      return with_client(out, [&](auto &client) {
        instserver::client::v1::GetBufferMetadataRequest req;
        req.set_buffer_id(argv[2]);
        auto resp =
            call_rpc(out, [&] { return client.get_buffer_metadata(req); });
        if (!resp.has_value()) {
          return;
        }
        const auto &meta = resp.value().meta();
        uint32_t element_count = meta.element_count();
        uint32_t data_type = meta.data_type();
        DataBuffer *buf = data_manager_get_buffer(argv[2]);
        if (buf == nullptr) {
          out.error("buffer data not found: " + std::string(argv[2]));
          return;
        }
        void *data = data_buffer_data(buf);
        if (data_type == INST_DATA_FLOAT64) {
          auto *ptr = static_cast<double *>(data);
          for (uint32_t k = 0; k < element_count; ++k) {
            std::cout << "[" << k << "] " << ptr[k] << "\n";
          }
        } else if (data_type == INST_DATA_FLOAT32) {
          auto *ptr = static_cast<float *>(data);
          for (uint32_t k = 0; k < element_count; ++k) {
            std::cout << "[" << k << "] " << ptr[k] << "\n";
          }
        } else if (data_type == INST_DATA_INT32) {
          auto *ptr = static_cast<int32_t *>(data);
          for (uint32_t k = 0; k < element_count; ++k) {
            std::cout << "[" << k << "] " << ptr[k] << "\n";
          }
        } else if (data_type == INST_DATA_INT64) {
          auto *ptr = static_cast<int64_t *>(data);
          for (uint32_t k = 0; k < element_count; ++k) {
            std::cout << "[" << k << "] " << ptr[k] << "\n";
          }
        } else if (data_type == INST_DATA_UINT8) {
          auto *ptr = static_cast<uint8_t *>(data);
          for (uint32_t k = 0; k < element_count; ++k) {
            std::cout << "[" << k << "] " << ptr[k] << "\n";
          }
        } else if (data_type == INST_DATA_UINT32) {
          auto *ptr = static_cast<uint32_t *>(data);
          for (uint32_t k = 0; k < element_count; ++k) {
            std::cout << "[" << k << "] " << ptr[k] << "\n";
          }
        } else if (data_type == INST_DATA_UINT64) {
          auto *ptr = static_cast<uint64_t *>(data);
          for (uint32_t k = 0; k < element_count; ++k) {
            std::cout << "[" << k << "] " << ptr[k] << "\n";
          }
        }
      });
    }
    case ISS_CLI_Command::RELEASE_BUFFER: {
      if (argc < 3) {
        out.error("Error: release-buffer requires buffer ID\n"
                  "Usage: instrument-script-server release-buffer <buffer_id> "
                  "[--json]");
        return out.emit();
      }
      return with_client(out, [&](auto &client) {
        instserver::client::v1::ReleaseBufferRequest req;
        req.set_buffer_id(argv[2]);
        auto resp = call_rpc(out, [&] { return client.release_buffer(req); });
        if (!resp.has_value()) {
          return;
        }
        out.message("Released buffer: " + std::string(argv[2]));
      });
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
      out.error("Unknown command: " + command);
      print_usage();
    }
    }
    return out.emit();
  } catch (const std::exception &e) {
    out.error(e.what());
  }

  return out.emit();
}
