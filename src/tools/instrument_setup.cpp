#include "instrument-server/Logger.hpp"
#include "instrument-server/plugin/PluginRegistry.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include <iostream>
#include <yaml-cpp/node/node.h>
#include <yaml-cpp/node/parse.h>

void print_usage(const char *prog) {
  std::cout << "Usage: " << prog << " <command> [options]\n\n";
  std::cout << "Commands:\n";
  std::cout << "  list-plugins              List all discovered plugins\n";
  std::cout << "  register-plugin           Register a plugin\n";
  std::cout << "    --protocol <type>       Protocol type\n";
  std::cout << "    --plugin <path>         Path to plugin . so/. dll\n";
  std::cout << "  validate                  Validate instrument config\n";
  std::cout << "    --config <path>         Path to config file\n";
  std::cout << "  test                      Test instrument communication\n";
  std::cout << "    --config <path>         Path to config file\n";
  std::cout << "    --command <verb>        Command to execute\n";
  std::cout << "  discover                  Discover plugins in directories\n";
  std::cout
      << "    --path <dir>            Search directory (can be repeated)\n";
}

int main(int argc, char **argv) {
  using namespace instserver;

  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  std::string command = argv[1];

  InstrumentLogger::instance().init("instrument_setup.log",
                                    spdlog::level::info);

  auto &plugin_registry = plugin::PluginRegistry::instance();

  if (command == "list-plugins") {
    std::vector<std::string> search_paths = {
        "/usr/local/lib/instrument-plugins", "/usr/lib/instrument-plugins",
        "./plugins"};
    plugin_registry.discover_plugins(search_paths);

    auto protocols = plugin_registry.list_protocols();

    std::cout << "Discovered " << protocols.size() << " plugins:\n\n";

    for (const auto &protocol : protocols) {
      std::cout << "  Protocol: " << protocol << "\n";
      std::cout << "  Path:      " << plugin_registry.get_plugin_path(protocol)
                << "\n\n";
    }

  } else if (command == "register-plugin") {
    std::string protocol;
    std::string plugin_path;

    for (int i = 2; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "--protocol" && i + 1 < argc) {
        protocol = argv[++i];
      } else if (arg == "--plugin" && i + 1 < argc) {
        plugin_path = argv[++i];
      }
    }

    if (protocol.empty() || plugin_path.empty()) {
      std::cerr << "Error: --protocol and --plugin are required\n";
      return 1;
    }

    if (plugin_registry.load_plugin(protocol, plugin_path)) {
      std::cout << "Successfully registered plugin for protocol: " << protocol
                << "\n";
    } else {
      std::cerr << "Failed to register plugin\n";
      return 1;
    }

  } else if (command == "validate") {
    std::string config_path;

    for (int i = 2; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "--config" && i + 1 < argc) {
        config_path = argv[++i];
      }
    }

    if (config_path.empty()) {
      std::cerr << "Error: --config is required\n";
      return 1;
    }

    // Validate config (basic YAML parsing)
    try {
      YAML::Node config = YAML::LoadFile(config_path);

      if (!config["name"]) {
        std::cerr << "Error: Config missing 'name' field\n";
        return 1;
      }

      if (!config["api_ref"]) {
        std::cerr << "Error:  Config missing 'api_ref' field\n";
        return 1;
      }

      if (!config["connection"]) {
        std::cerr << "Error: Config missing 'connection' field\n";
        return 1;
      }

      std::cout << "Config validation passed:  " << config_path << "\n";
      std::cout << "  Instrument:  " << config["name"].as<std::string>()
                << "\n";
      std::cout << "  API:         " << config["api_ref"].as<std::string>()
                << "\n";

    } catch (const std::exception &ex) {
      std::cerr << "Config validation failed:  " << ex.what() << "\n";
      return 1;
    }

  } else if (command == "test") {
    std::string config_path;
    std::string command_verb;

    for (int i = 2; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "--config" && i + 1 < argc) {
        config_path = argv[++i];
      } else if (arg == "--command" && i + 1 < argc) {
        command_verb = argv[++i];
      }
    }

    if (config_path.empty()) {
      std::cerr << "Error: --config is required\n";
      return 1;
    }

    if (command_verb.empty()) {
      std::cerr << "Error: --command is required\n";
      return 1;
    }

    // Load instrument
    auto &registry = InstrumentRegistry::instance();

    if (!registry.create_instrument(config_path)) {
      std::cerr << "Failed to create instrument\n";
      return 1;
    }

    auto instruments = registry.list_instruments();
    if (instruments.empty()) {
      std::cerr << "No instruments created\n";
      return 1;
    }

    std::string instrument_name = instruments[0];
    auto proxy = registry.get_instrument(instrument_name);

    std::cout << "Testing instrument: " << instrument_name << "\n";
    std::cout << "Executing command: " << command_verb << "\n";

    // Execute test command
    SerializedCommand cmd;
    cmd.id = "test-1";
    cmd.instrument_name = instrument_name;
    cmd.verb = command_verb;
    cmd.expects_response = true;

    auto resp =
        proxy->execute_sync(std::move(cmd), std::chrono::milliseconds(5000));

    std::cout << "\nResult:\n";
    std::cout << "  Success: " << (resp.success ? "YES" : "NO") << "\n";
    if (!resp.success) {
      std::cout << "  Error:    " << resp.error_message << "\n";
    }
    if (!resp.text_response.empty()) {
      std::cout << "  Response: " << resp.text_response << "\n";
    }

    registry.stop_all();

  } else if (command == "discover") {
    std::vector<std::string> search_paths;

    for (int i = 2; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "--path" && i + 1 < argc) {
        search_paths.push_back(argv[++i]);
      }
    }

    if (search_paths.empty()) {
      search_paths = {"/usr/local/lib/instrument-plugins",
                      "/usr/lib/instrument-plugins", "./plugins"};
    }

    std::cout << "Discovering plugins in:\n";
    for (const auto &path : search_paths) {
      std::cout << "  " << path << "\n";
    }
    std::cout << "\n";

    plugin_registry.discover_plugins(search_paths);

    auto protocols = plugin_registry.list_protocols();
    std::cout << "Found " << protocols.size() << " plugins\n";

  } else {
    std::cerr << "Unknown command: " << command << "\n";
    print_usage(argv[0]);
    return 1;
  }

  return 0;
}
