#include "instrument-server/Logger.hpp"
#include "instrument-server/SerializedCommand.hpp"
#include "instrument-server/plugin/PluginRegistry.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/RuntimeContext.hpp"
#include "instrument-server/server/ServerDaemon.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"
#include <csignal>
#include <filesystem>
#include <iostream>
#include <sol/sol.hpp>
#include <string>
#include <vector>

using namespace instserver;

static volatile bool g_running = true;

void signal_handler(int sig) {
  (void)sig;
  g_running = false;
}

void print_usage() {
  std::cout << "Usage: instrument-server <command> [options]\n\n";
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
  std::cout
      << "  measure <script>                   Run Lua measurement script\n";
  std::cout << "\nUtilities:\n";
  std::cout << "  test <config> <verb> [params]      Test command\n";
  std::cout << "  discover [paths...]                Discover plugins\n";
  std::cout << "  plugins                            List available plugins\n";
  std::cout << "\nOptions:\n";
  std::cout << "  --plugin <path>      Custom plugin (. so/. dll)\n";
  std::cout << "  --log-level <level>  Log level (default: info)\n";
  std::cout << "\nWorkflow:\n";
  std::cout << "  1. Start daemon:\n";
  std::cout << "     instrument-server daemon start\n";
  std::cout << "\n  2. Start instruments:\n";
  std::cout << "     instrument-server start dac1. yaml\n";
  std::cout << "     instrument-server start dmm1.yaml\n";
  std::cout
      << "     instrument-server start scope1.yaml --plugin ./custom.so\n";
  std::cout << "\n  3. Run measurement:\n";
  std::cout << "     instrument-server measure my_measurement.lua\n";
  std::cout << "\n  4. Manage:\n";
  std::cout << "     instrument-server list\n";
  std::cout << "     instrument-server status DAC1\n";
  std::cout << "     instrument-server stop DAC1\n";
  std::cout << "\n  5. Shutdown:\n";
  std::cout << "     instrument-server daemon stop\n";
}

spdlog::level::level_enum parse_log_level(const std::string &level) {
  if (level == "debug")
    return spdlog::level::debug;
  if (level == "warn")
    return spdlog::level::warn;
  if (level == "error")
    return spdlog::level::err;
  if (level == "trace")
    return spdlog::level::trace;
  return spdlog::level::info;
}

void init_plugins(const std::string &custom_plugin = "") {
  auto &plugin_registry = plugin::PluginRegistry::instance();
  std::vector<std::string> plugin_paths = {"/usr/local/lib/instrument-plugins",
                                           "/usr/lib/instrument-plugins",
                                           "./plugins", "."};
  plugin_registry.discover_plugins(plugin_paths);

  if (!custom_plugin.empty()) {
    if (!std::filesystem::exists(custom_plugin)) {
      throw std::runtime_error("Plugin file not found: " + custom_plugin);
    }

    plugin::PluginLoader loader(custom_plugin);
    if (loader.is_loaded()) {
      auto metadata = loader.get_metadata();
      plugin_registry.load_plugin(metadata.protocol_type, custom_plugin);
    } else {
      throw std::runtime_error("Failed to load plugin:  " + custom_plugin);
    }
  }
}

bool ensure_daemon_running() {
  if (!ServerDaemon::is_already_running()) {
    std::cerr << "Error: Server daemon is not running\n";
    std::cerr << "Start it with: instrument-server daemon start\n";
    return false;
  }
  return true;
}

int cmd_daemon(int argc, char **argv);
int cmd_start(int argc, char **argv);
int cmd_stop(int argc, char **argv);
int cmd_status(int argc, char **argv);
int cmd_list(int argc, char **argv);
int cmd_measure(int argc, char **argv);
int cmd_test(int argc, char **argv);
int cmd_discover(int argc, char **argv);
int cmd_plugins(int argc, char **argv);

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  std::string command = argv[1];

  if (command == "daemon") {
    return cmd_daemon(argc - 2, argv + 2);
  } else if (command == "start") {
    return cmd_start(argc - 2, argv + 2);
  } else if (command == "stop") {
    return cmd_stop(argc - 2, argv + 2);
  } else if (command == "status") {
    return cmd_status(argc - 2, argv + 2);
  } else if (command == "list") {
    return cmd_list(argc - 2, argv + 2);
  } else if (command == "measure") {
    return cmd_measure(argc - 2, argv + 2);
  } else if (command == "test") {
    return cmd_test(argc - 2, argv + 2);
  } else if (command == "discover") {
    return cmd_discover(argc - 2, argv + 2);
  } else if (command == "plugins") {
    return cmd_plugins(argc - 2, argv + 2);
  } else if (command == "--help" || command == "-h") {
    print_usage();
    return 0;
  } else {
    std::cerr << "Unknown command: " << command << "\n\n";
    print_usage();
    return 1;
  }
}

int cmd_daemon(int argc, char **argv) {
  if (argc < 1) {
    std::cerr << "Usage: instrument-server daemon <start|stop|status>\n";
    return 1;
  }

  std::string subcmd = argv[0];
  std::string log_level = "info";

  // Parse options
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--log-level" && i + 1 < argc) {
      log_level = argv[++i];
    }
  }

  if (subcmd == "start") {
    // Check if already running
    if (ServerDaemon::is_already_running()) {
      std::cout << "Server daemon is already running (PID: "
                << ServerDaemon::get_daemon_pid() << ")\n";
      return 0;
    }

    // Initialize logger
    InstrumentLogger::instance().init("instrument_server. log",
                                      parse_log_level(log_level));

    LOG_INFO("MAIN", "DAEMON_START", "Starting server daemon");

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    auto &daemon = ServerDaemon::instance();
    if (!daemon.start()) {
      std::cerr << "Failed to start server daemon\n";
      return 1;
    }

    std::cout << "Server daemon started (PID: "
              << ServerDaemon::get_daemon_pid() << ")\n";
    std::cout << "Daemon running in background\n";

    // Keep main thread alive while daemon runs
    while (g_running && daemon.is_running()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    daemon.stop();
    return 0;

  } else if (subcmd == "stop") {
    if (!ServerDaemon::is_already_running()) {
      std::cout << "Server daemon is not running\n";
      return 0;
    }

    int pid = ServerDaemon::get_daemon_pid();
    std::cout << "Stopping server daemon (PID: " << pid << ")...\n";

#ifdef _WIN32
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (process) {
      TerminateProcess(process, 0);
      CloseHandle(process);
      std::cout << "Server daemon stopped\n";
    } else {
      std::cerr << "Failed to stop daemon\n";
      return 1;
    }
#else
    if (kill(pid, SIGTERM) == 0) {
      std::cout << "Server daemon stopped\n";
    } else {
      std::cerr << "Failed to stop daemon\n";
      return 1;
    }
#endif

    return 0;

  } else if (subcmd == "status") {
    if (ServerDaemon::is_already_running()) {
      int pid = ServerDaemon::get_daemon_pid();
      std::cout << "Server daemon is running (PID: " << pid << ")\n";

      // Show runtime directory
      std::cout << "Runtime directory: " << ServerDaemon::get_pid_file_path()
                << "\n";

      return 0;
    } else {
      std::cout << "Server daemon is not running\n";
      return 1;
    }

  } else {
    std::cerr << "Unknown daemon command: " << subcmd << "\n";
    std::cerr << "Usage: instrument-server daemon <start|stop|status>\n";
    return 1;
  }
}

int cmd_start(int argc, char **argv) {
  if (argc < 1) {
    std::cerr << "Error: start requires config file\n";
    std::cerr << "Usage: instrument-server start <config> [--plugin <path>] "
                 "[--log-level <level>]\n";
    return 1;
  }

  if (!ensure_daemon_running()) {
    return 1;
  }

  std::string config_path = argv[0];
  std::string custom_plugin;
  std::string log_level = "info";

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--plugin" && i + 1 < argc) {
      custom_plugin = argv[++i];
    } else if (arg == "--log-level" && i + 1 < argc) {
      log_level = argv[++i];
    }
  }

  InstrumentLogger::instance().init("instrument_server.log",
                                    parse_log_level(log_level));

  try {
    init_plugins(custom_plugin);

    auto &registry = InstrumentRegistry::instance();

    if (!registry.create_instrument(config_path)) {
      std::cerr << "Failed to create instrument from:  " << config_path << "\n";
      return 1;
    }

    auto instruments = registry.list_instruments();
    std::string instrument_name = instruments.back();

    std::cout << "Started instrument: " << instrument_name << "\n";

    return 0;

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}

int cmd_stop(int argc, char **argv) {
  if (argc < 1) {
    std::cerr << "Error: stop requires instrument name\n";
    std::cerr << "Usage: instrument-server stop <name>\n";
    return 1;
  }

  if (!ensure_daemon_running()) {
    return 1;
  }

  std::string name = argv[0];

  auto &registry = InstrumentRegistry::instance();

  if (!registry.has_instrument(name)) {
    std::cerr << "Instrument not found: " << name << "\n";
    return 1;
  }

  registry.remove_instrument(name);
  std::cout << "Stopped instrument:  " << name << "\n";

  return 0;
}

int cmd_status(int argc, char **argv) {
  if (argc < 1) {
    std::cerr << "Error: status requires instrument name\n";
    std::cerr << "Usage: instrument-server status <name>\n";
    return 1;
  }

  if (!ensure_daemon_running()) {
    return 1;
  }

  std::string name = argv[0];

  auto &registry = InstrumentRegistry::instance();
  auto proxy = registry.get_instrument(name);

  if (!proxy) {
    std::cerr << "Instrument not found: " << name << "\n";
    return 1;
  }

  std::cout << "Instrument:  " << name << "\n";
  std::cout << "  Status: " << (proxy->is_alive() ? "RUNNING" : "STOPPED")
            << "\n";

  auto stats = proxy->get_stats();
  std::cout << "  Commands sent: " << stats.commands_sent << "\n";
  std::cout << "  Commands completed: " << stats.commands_completed << "\n";
  std::cout << "  Commands failed: " << stats.commands_failed << "\n";
  std::cout << "  Commands timeout: " << stats.commands_timeout << "\n";

  return 0;
}

int cmd_list(int argc, char **argv) {
  (void)argc;
  (void)argv;

  if (!ensure_daemon_running()) {
    return 1;
  }

  auto &registry = InstrumentRegistry::instance();
  auto instruments = registry.list_instruments();

  if (instruments.empty()) {
    std::cout << "No instruments running\n";
    return 0;
  }

  std::cout << "Running instruments:\n";
  for (const auto &name : instruments) {
    auto proxy = registry.get_instrument(name);
    std::string status = proxy && proxy->is_alive() ? "RUNNING" : "STOPPED";
    std::cout << "  " << name << " [" << status << "]\n";
  }

  return 0;
}

int cmd_measure(int argc, char **argv) {
  if (argc < 1) {
    std::cerr << "Error: measure requires script path\n";
    std::cerr
        << "Usage: instrument-server measure <script> [--log-level <level>]\n";
    return 1;
  }

  if (!ensure_daemon_running()) {
    return 1;
  }

  std::string script_path = argv[0];
  std::string log_level = "info";

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--log-level" && i + 1 < argc) {
      log_level = argv[++i];
    }
  }

  InstrumentLogger::instance().init("instrument_server. log",
                                    parse_log_level(log_level));

  try {
    auto &registry = InstrumentRegistry::instance();

    auto instruments = registry.list_instruments();
    if (instruments.empty()) {
      std::cerr << "Error:  No instruments running\n";
      std::cerr
          << "Start instruments first:  instrument-server start <config>\n";
      return 1;
    }

    LOG_INFO("SERVER", "MEASURE", "Script: {}", script_path);

    // Setup Lua
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                       sol::lib::string, sol::lib::io, sol::lib::os);

    SyncCoordinator sync_coordinator;
    bind_runtime_context(lua, registry, sync_coordinator);

    // Create default context
    RuntimeContext ctx(registry, sync_coordinator);
    lua["context"] = &ctx;

    std::cout << "Running measurement.. .\n";

    auto result = lua.safe_script_file(script_path);

    if (!result.valid()) {
      sol::error err = result;
      std::cerr << "Script error: " << err.what() << "\n";
      return 1;
    }

    std::cout << "Measurement complete\n";
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}

int cmd_test(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Error: test requires config and verb\n";
    std::cerr << "Usage: instrument-server test <config> <verb> "
                 "[param=value... ] [--plugin <path>]\n";
    return 1;
  }

  std::string config_path = argv[0];
  std::string verb = argv[1];
  std::string custom_plugin;
  std::string log_level = "info";
  int param_start = 2;

  for (int i = 2; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--plugin" && i + 1 < argc) {
      custom_plugin = argv[++i];
      param_start = i + 1;
    } else if (arg == "--log-level" && i + 1 < argc) {
      log_level = argv[++i];
      param_start = i + 1;
    }
  }

  InstrumentLogger::instance().init("instrument_server.log",
                                    parse_log_level(log_level));

  try {
    init_plugins(custom_plugin);

    auto &registry = InstrumentRegistry::instance();

    if (!registry.create_instrument(config_path)) {
      std::cerr << "Failed to create instrument\n";
      return 1;
    }

    auto instruments = registry.list_instruments();
    std::string instrument_name = instruments.back();
    auto proxy = registry.get_instrument(instrument_name);

    std::cout << "Testing:  " << instrument_name << "\n";
    std::cout << "Command: " << verb << "\n";

    SerializedCommand cmd;
    cmd.id = "test-cmd";
    cmd.instrument_name = instrument_name;
    cmd.verb = verb;
    cmd.expects_response = true;

    for (int i = param_start; i < argc; i++) {
      std::string arg = argv[i];
      if (arg.find("--") == 0)
        continue;

      size_t eq_pos = arg.find('=');
      if (eq_pos != std::string::npos) {
        std::string key = arg.substr(0, eq_pos);
        std::string value = arg.substr(eq_pos + 1);

        try {
          cmd.params[key] = std::stod(value);
        } catch (...) {
          cmd.params[key] = value;
        }
      }
    }

    auto resp =
        proxy->execute_sync(std::move(cmd), std::chrono::milliseconds(5000));

    std::cout << "\nResult:\n";
    std::cout << "  Success: " << (resp.success ? "YES" : "NO") << "\n";
    if (!resp.success) {
      std::cout << "  Error: " << resp.error_message << "\n";
    }
    if (!resp.text_response.empty()) {
      std::cout << "  Response: " << resp.text_response << "\n";
    }

    registry.remove_instrument(instrument_name);

    return resp.success ? 0 : 1;

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}

int cmd_discover(int argc, char **argv) {
  std::vector<std::string> search_paths;

  if (argc > 0) {
    for (int i = 0; i < argc; i++) {
      search_paths.push_back(argv[i]);
    }
  } else {
    search_paths = {"/usr/local/lib/instrument-plugins",
                    "/usr/lib/instrument-plugins", "./plugins", "."};
  }

  auto &plugin_registry = plugin::PluginRegistry::instance();

  std::cout << "Discovering plugins in:\n";
  for (const auto &path : search_paths) {
    std::cout << "  " << path << "\n";
  }
  std::cout << "\n";

  plugin_registry.discover_plugins(search_paths);

  auto protocols = plugin_registry.list_protocols();

  std::cout << "Found " << protocols.size() << " plugin(s):\n\n";

  for (const auto &protocol : protocols) {
    std::string plugin_path = plugin_registry.get_plugin_path(protocol);

    std::cout << "Protocol: " << protocol << "\n";
    std::cout << "  Path: " << plugin_path << "\n";

    try {
      plugin::PluginLoader loader(plugin_path);
      if (loader.is_loaded()) {
        auto metadata = loader.get_metadata();
        std::cout << "  Name: " << metadata.name << "\n";
        std::cout << "  Version: " << metadata.version << "\n";
        std::cout << "  Description: " << metadata.description << "\n";
      }
    } catch (...) {
      std::cout << "  (Failed to load metadata)\n";
    }

    std::cout << "\n";
  }

  return 0;
}

int cmd_plugins(int argc, char **argv) {
  (void)argc;
  (void)argv;

  auto &plugin_registry = plugin::PluginRegistry::instance();
  std::vector<std::string> plugin_paths = {"/usr/local/lib/instrument-plugins",
                                           "/usr/lib/instrument-plugins",
                                           "./plugins", "."};
  plugin_registry.discover_plugins(plugin_paths);

  auto protocols = plugin_registry.list_protocols();

  if (protocols.empty()) {
    std::cout << "No plugins found\n";
    return 0;
  }

  std::cout << "Available plugins:\n\n";

  for (const auto &protocol : protocols) {
    std::string plugin_path = plugin_registry.get_plugin_path(protocol);
    std::cout << "  " << protocol << " -> " << plugin_path << "\n";
  }

  std::cout << "\nTotal:  " << protocols.size() << " plugin(s)\n";

  return 0;
}
