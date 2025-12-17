#include "instrument-server/Logger.hpp"
#include "instrument-server/plugin/PluginRegistry.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/RuntimeContext.hpp"
#include <atomic>
#include <csignal>
#include <sol/sol.hpp>

static std::atomic<bool> g_running{true};

void signal_handler(int signal) { g_running = false; }

int main(int argc, char **argv) {
  using namespace instserver;

  // Parse command line
  std::string config_file;
  std::string script_file;
  std::string log_level = "info";

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--config" && i + 1 < argc) {
      config_file = argv[++i];
    } else if (arg == "--script" && i + 1 < argc) {
      script_file = argv[++i];
    } else if (arg == "--log-level" && i + 1 < argc) {
      log_level = argv[++i];
    }
  }

  if (config_file.empty() || script_file.empty()) {
    fprintf(stderr,
            "Usage: %s --config <config. yaml> --script <script.lua> "
            "[--log-level debug|info|warn|error]\n",
            argv[0]);
    return 1;
  }

  // Initialize logger
  spdlog::level::level_enum level = spdlog::level::info;
  if (log_level == "debug")
    level = spdlog::level::debug;
  else if (log_level == "trace")
    level = spdlog::level::trace;
  else if (log_level == "warn")
    level = spdlog::level::warn;
  else if (log_level == "error")
    level = spdlog::level::err;

  InstrumentLogger::instance().init("instrument_server. log", level);

  LOG_INFO("MAIN", "STARTUP", "Instrument Server starting");
  LOG_INFO("MAIN", "STARTUP", "Config:  {}", config_file);
  LOG_INFO("MAIN", "STARTUP", "Script: {}", script_file);

  // Install signal handlers
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);

  try {
    // Discover plugins in standard directories
    auto &plugin_registry = plugin::PluginRegistry::instance();
    std::vector<std::string> plugin_paths = {
        "/usr/local/lib/instrument-plugins", "/usr/lib/instrument-plugins",
        "./plugins"};
    plugin_registry.discover_plugins(plugin_paths);

    LOG_INFO("MAIN", "STARTUP", "Discovered {} plugins",
             plugin_registry.list_protocols().size());

    // Load instrument configurations
    auto &registry = InstrumentRegistry::instance();

    // Parse config file (can be single instrument or list)
    if (!registry.create_instrument(config_file)) {
      LOG_ERROR("MAIN", "STARTUP", "Failed to create instruments from config");
      return 1;
    }

    LOG_INFO("MAIN", "STARTUP", "Created {} instruments",
             registry.list_instruments().size());

    // Setup Lua environment
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                       sol::lib::string);

    // Bind runtime contexts
    bind_runtime_contexts(lua);

    // Create runtime context (detect type from script or config)
    // For now, default to 1D waveform
    RuntimeContext_1DWaveform ctx(registry);
    lua["context"] = &ctx;

    LOG_INFO("MAIN", "STARTUP", "Lua environment initialized");

    // Run script
    LOG_INFO("MAIN", "SCRIPT", "Executing measurement script");

    auto result = lua.safe_script_file(script_file);

    if (!result.valid()) {
      sol::error err = result;
      LOG_ERROR("MAIN", "SCRIPT", "Lua error: {}", err.what());
      return 1;
    }

    LOG_INFO("MAIN", "SCRIPT", "Script completed successfully");

    // Wait for shutdown signal
    LOG_INFO("MAIN", "RUNNING", "Server running, press Ctrl+C to stop");

    while (g_running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_INFO("MAIN", "SHUTDOWN", "Shutting down");

    // Stop all instruments
    registry.stop_all();

  } catch (const std::exception &ex) {
    LOG_ERROR("MAIN", "FATAL", "Fatal exception: {}", ex.what());
    return 1;
  }

  LOG_INFO("MAIN", "SHUTDOWN", "Server stopped");
  return 0;
}
