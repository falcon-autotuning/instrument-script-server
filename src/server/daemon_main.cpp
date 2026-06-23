#include "instrument-script-server/server/ServerDaemon.hpp"
#include <inst_logging.h>

#include <csignal>
#include <cstdlib>
#include <cxxopts.hpp>
#include <iostream>
#include <thread>

using namespace instserver;

constexpr int DEFAULT_PORT = 8555;

namespace {
static volatile bool g_running = true;

void signal_handler(int sig) {
  (void)sig;
  g_running = false;
}

uint8_t parse_log_level(const std::string &log_level) {
  if (log_level == "trace") {
    return INST_LOG_TRACE;
  }
  if (log_level == "debug") {
    return INST_LOG_DEBUG;
  }
  if (log_level == "info") {
    return INST_LOG_INFO;
  }
  if (log_level == "warn") {
    return INST_LOG_WARN;
  }
  if (log_level == "error") {
    return INST_LOG_ERROR;
  }

  throw std::runtime_error("Invalid log level: " + log_level);
}
} // namespace

int main(int argc, char **argv) {
  try {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    cxxopts::Options options("instrument-script-server-daemon",
                             "Instrument Script Server Daemon");

    options.add_options()("h,help", "Show help")("version", "Show version")(
        "log-level", "Log level (trace|debug|info|warn|error)",
        cxxopts::value<std::string>()->default_value("info"));

    auto result = options.parse(argc, argv);

    if (result.contains("help")) {
      std::cout << options.help() << "\n";
      return 0;
    }

    if (result.contains("version")) {
      std::cout << "instrument-script-server version 1.2.0\n";
      return 0;
    }

    std::string log_level_str = result["log-level"].as<std::string>();
    uint8_t level = parse_log_level(log_level_str);

    inst_log_init("instrument_server.log", level, "instrument",
                  10 * 1024 * 1024, 3);

    LOG_INFO("DAEMON", "MAIN", "Starting daemon process");

    auto &daemon = ServerDaemon::instance();

    const char *rpc_port_env = std::getenv("INSTRUMENT_SCRIPT_SERVER_RPC_PORT");

    uint16_t port = DEFAULT_PORT;

    if ((rpc_port_env != nullptr) && rpc_port_env[0] != '\0') {
      try {
        int env_port = std::stoi(rpc_port_env);
        if (env_port > 0 && env_port <= 65535) {
          port = static_cast<uint16_t>(env_port);
        }
      } catch (...) {
        LOG_WARN("DAEMON", "CONFIG", "Invalid port in env, using default");
      }
    }

    daemon.set_rpc_port(port);

    if (!daemon.start()) {
      LOG_ERROR("DAEMON", "START", "Failed to start daemon");
      std::cerr << "daemon start failed\n";
      return 1;
    }

    LOG_INFO("DAEMON", "RUN", "Daemon running on port %d", port);

    while (g_running && daemon.is_running()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_INFO("DAEMON", "SHUTDOWN", "Stopping daemon");

    daemon.stop();
    return 0;

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
