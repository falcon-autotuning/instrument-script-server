#include "instrument-script-server/server/CommandHandlers.hpp"
#include "instrument-script-server/version.hpp"
#include <iostream>
#include <spdlog/spdlog.h>

using namespace instserver;

static volatile bool g_running = true;

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
               "[--block_inject_globals]"
               "[--context_schema_version <x.y.z>]"
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
  std::cout << "  --version            Show version information\n";
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
} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  std::string command = argv[1];
  int rc;
  nlohmann::json params;
  nlohmann::json out;
  switch (parse_command(command)) {
  case ISS_CLI_Command::DAEMON: {
    // subcommand is positional 0
    if (argc < 3) {
      std::cerr
          << "Usage: instrument-script-server daemon <start|stop|status>\n";
      rc = 1;
      break;
    }
    std::string action = argv[2];
    params["action"] = action;

    // parse options
    bool json_output = false;
    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--log-level" && i + 1 < argc) {
        params["log_level"] = argv[++i];
      }
      if (arg == "--json") {
        json_output = true;
      }
    }

    int rc = server::handle_daemon(params, out);
    if (out.is_null()) {
      break;
    }
    if (json_output) {
      std::cout << out.dump() << "\n";
      break;
    }
    if (out.contains("error")) {
      std::cerr << out["error"].get<std::string>() << "\n";
    } else if (out.contains("message")) {
      std::cout << out["message"].get<std::string>() << "\n";
    }
    break;
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
