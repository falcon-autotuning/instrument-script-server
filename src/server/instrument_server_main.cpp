#include "instrument-server/server/CommandHandlers.hpp"
#include <iostream>
#include <spdlog/spdlog.h>

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
  std::cout << "     instrument-server start dac1.yaml\n";
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

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  std::string command = argv[1];

  // Dispatch mapping CLI commands to handlers
  if (command == "daemon") {
    // subcommand is positional 0
    if (argc < 3) {
      std::cerr << "Usage: instrument-server daemon <start|stop|status>\n";
      return 1;
    }
    std::string action = argv[2];
    nlohmann::json params;
    params["action"] = action;

    // parse options
    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--log-level" && i + 1 < argc) {
        params["log_level"] = argv[++i];
      }
    }

    nlohmann::json out;
    int rc = server::handle_daemon(params, out);
    if (!out.is_null()) {
      if (out.contains("error"))
        std::cerr << out["error"].get<std::string>() << "\n";
      else if (out.contains("message"))
        std::cout << out["message"].get<std::string>() << "\n";
    }
    return rc;
  } else if (command == "start") {
    if (argc < 2) {
      std::cerr << "Usage: instrument-server start <config> [--plugin <path>] "
                   "[--log-level <level>]\n";
      return 1;
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
    int rc = server::handle_start(params, out);
    if (!out.is_null()) {
      if (out.contains("error"))
        std::cerr << out["error"].get<std::string>() << "\n";
      if (out.contains("instrument"))
        std::cout << "Started instrument: "
                  << out["instrument"].get<std::string>() << "\n";
    }
    return rc;
  } else if (command == "stop") {
    if (argc < 3) {
      std::cerr << "Error: stop requires instrument name\n";
      std::cerr << "Usage: instrument-server stop <name>\n";
      return 1;
    }
    nlohmann::json params;
    params["name"] = argv[2];
    nlohmann::json out;
    int rc = server::handle_stop(params, out);
    if (!out.is_null()) {
      if (out.contains("error"))
        std::cerr << out["error"].get<std::string>() << "\n";
      else
        std::cout << "Stopped instrument: " << params["name"].get<std::string>()
                  << "\n";
    }
    return rc;
  } else if (command == "status") {
    if (argc < 3) {
      std::cerr << "Error: status requires instrument name\n";
      std::cerr << "Usage: instrument-server status <name>\n";
      return 1;
    }
    nlohmann::json params;
    params["name"] = argv[2];
    nlohmann::json out;
    int rc = server::handle_status(params, out);
    if (!out.is_null()) {
      if (out.contains("error"))
        std::cerr << out["error"].get<std::string>() << "\n";
      else {
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
    return rc;
  } else if (command == "list") {
    nlohmann::json params;
    nlohmann::json out;
    int rc = server::handle_list(params, out);
    if (!out.is_null() && out.contains("instruments")) {
      auto arr = out["instruments"];
      if (arr.empty()) {
        std::cout << "No instruments running\n";
        return 1;
      } else {
        std::cout << "Running instruments:\n";
        for (auto &name : arr)
          std::cout << "  " << name.get<std::string>() << "\n";
        return 0;
      }
    }
    return rc;
  } else if (command == "measure") {
    if (argc < 3) {
      std::cerr << "Error: measure requires script path\n";
      std::cerr << "Usage: instrument-server measure <script> [--json] "
                   "[--log-level <level>]\n";
      return 1;
    }
    nlohmann::json params;
    params["script_path"] = argv[2];
    for (int i = 3; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--log-level" && i + 1 < argc) {
        params["log_level"] = argv[++i];
      } else if (arg == "--json") {
        params["json"] = true;
      }
    }
    nlohmann::json out;
    int rc = server::handle_measure(params, out);
    if (!out.is_null()) {
      if (!out.value("ok", false)) {
        std::cerr << out.value("error", "measure failed") << "\n";
      } else if (params.value("json", false)) {
        std::cout << out.dump(2) << "\n";
      } else {
        std::cout << "Measurement complete\n";
      }
    }
    return rc;
  } else if (command == "test") {
    if (argc < 3) {
      std::cerr << "Error: test requires config and verb\n";
      std::cerr
          << "Usage: instrument-server test <config> <verb> [param=value... ] "
             "[--plugin <path>] [--log-level <level>]\n";
      return 1;
    }
    nlohmann::json params;
    params["config_path"] = argv[2];
    params["verb"] = argv[3];
    // parse remaining args as key=value or options
    for (int i = 4; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--plugin" && i + 1 < argc) {
        params["plugin"] = argv[++i];
        continue;
      }
      if (arg == "--log-level" && i + 1 < argc) {
        params["log_level"] = argv[++i];
        continue;
      }
      size_t eq = arg.find('=');
      if (eq != std::string::npos) {
        std::string key = arg.substr(0, eq);
        std::string val = arg.substr(eq + 1);
        // try number parse
        try {
          if (val.find('.') != std::string::npos)
            params["params"][key] = std::stod(val);
          else
            params["params"][key] = std::stoll(val);
        } catch (...) {
          if (val == "true" || val == "false") {
            params["params"][key] = (val == "true");
          } else {
            params["params"][key] = val;
          }
        }
      }
    }

    nlohmann::json out;
    int rc = server::handle_test(params, out);
    if (!out.is_null()) {
      if (!out.value("ok", false)) {
        std::cerr << out.value("error", "test failed") << "\n";
      } else {
        if (out.contains("text_response"))
          std::cout << out["text_response"].get<std::string>() << "\n";
      }
    }
    return rc;
  } else if (command == "discover") {
    nlohmann::json params;
    if (argc > 2) {
      params["paths"] = nlohmann::json::array();
      for (int i = 2; i < argc; ++i)
        params["paths"].push_back(argv[i]);
    }
    nlohmann::json out;
    int rc = server::handle_discover(params, out);
    if (!out.is_null()) {
      if (out.contains("protocols")) {
        auto p = out["protocols"];
        std::cout << "Found " << p.size() << " plugin(s):\n";
        for (auto &proto : p)
          std::cout << "  " << proto.get<std::string>() << "\n";
      }
    }
    return rc;
  } else if (command == "plugins") {
    nlohmann::json params;
    nlohmann::json out;
    int rc = server::handle_plugins(params, out);
    if (!out.is_null()) {
      if (out.contains("plugins")) {
        auto arr = out["plugins"];
        if (arr.empty()) {
          std::cout << "No plugins found\n";
        } else {
          std::cout << "Available plugins:\n\n";
          for (auto &p : arr) {
            std::cout << "  " << p["protocol"].get<std::string>() << " -> "
                      << p["path"].get<std::string>() << "\n";
          }
        }
      }
    }
    return rc;
  } else if (command == "--help" || command == "-h") {
    print_usage();
    return 0;
  } else {
    std::cerr << "Unknown command: " << command << "\n\n";
    print_usage();
    return 1;
  }
}
