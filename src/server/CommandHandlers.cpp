#include "instrument-server/server/CommandHandlers.hpp"
#include "instrument-server/Logger.hpp"
#include "instrument-server/SerializedCommand.hpp"
#include "instrument-server/plugin/PluginLoader.hpp"
#include "instrument-server/plugin/PluginRegistry.hpp"
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/JobManager.hpp"
#include "instrument-server/server/RuntimeContext.hpp"
#include "instrument-server/server/ServerDaemon.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"
#include <sol/sol.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace instserver {
namespace server {

/*
  Each handler expects a params JSON object with keys as described below
  and fills `out` with a JSON response containing at minimum {"ok": true|false}
*/

int handle_daemon(const json &params, json &out) {
  out = json::object();
  std::string action = params.value("action", "");
  std::string log_level = params.value("log_level", "info");
  bool block = params.value("block", true); // block by default for CLI usage

  auto &daemon = ServerDaemon::instance();

  if (action == "start") {
    // Initialize logger (same default as CLI)
    InstrumentLogger::instance().init("instrument_server.log",
                                      spdlog::level::from_str(log_level));

    // Check for RPC port configuration from environment variable
    const char *rpc_port_env = std::getenv("INSTRUMENT_SERVER_RPC_PORT");
    if (rpc_port_env && rpc_port_env[0]) {
      try {
        int port = std::stoi(rpc_port_env);
        if (port > 0 && port <= 65535) {
          daemon.set_rpc_port(static_cast<uint16_t>(port));
        }
      } catch (...) {
        // Invalid port number, ignore
      }
    }

    if (!daemon.start()) {
      out["ok"] = false;
      out["error"] = "Failed to start daemon";
      return 1;
    }
    out["ok"] = true;
    out["pid"] = ServerDaemon::get_daemon_pid();
    
    // Block and wait for daemon to stop (keeps process alive)
    // This is necessary for CLI usage to keep the daemon process running
    if (block) {
      while (daemon.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
    }
    
    return 0;
  } else if (action == "stop") {
    if (!ServerDaemon::is_already_running()) {
      out["ok"] = true;
      out["message"] = "daemon not running";
      return 0;
    }
    daemon.stop();
    out["ok"] = true;
    return 0;
  } else if (action == "status") {
    bool running = ServerDaemon::is_already_running();
    out["ok"] = true;
    out["running"] = running;
    if (running) {
      out["pid"] = ServerDaemon::get_daemon_pid();
      out["message"] =
          fmt::format("daemon running (pid={})", out["pid"].get<int>());
    } else {
      out["message"] = "daemon not running";
    }
    return 0;
  }

  out["ok"] = false;
  out["error"] = "Unknown daemon action";
  return 1;
}

int handle_start(const json &params, json &out) {
  out = json::object();
  std::string config_path = params.value("config_path", "");
  std::string custom_plugin = params.value("plugin", "");
  std::string log_level = params.value("log_level", "info");

  if (config_path.empty()) {
    out["ok"] = false;
    out["error"] = "missing config_path";
    return 1;
  }

  InstrumentLogger::instance().init("instrument_server.log",
                                    spdlog::level::from_str(log_level));

  try {
    // If a custom plugin path was provided, try to load it via PluginLoader.
    if (!custom_plugin.empty()) {
      if (!std::filesystem::exists(custom_plugin)) {
        out["ok"] = false;
        out["error"] = "plugin file not found";
        return 1;
      }

      plugin::PluginLoader loader(custom_plugin);
      if (!loader.is_loaded()) {
        out["ok"] = false;
        out["error"] = "failed to load plugin";
        return 1;
      }

      auto metadata = loader.get_metadata();
      plugin::PluginRegistry::instance().load_plugin(metadata.protocol_type,
                                                     custom_plugin);
    }

    auto &registry = InstrumentRegistry::instance();
    bool ok = registry.create_instrument(config_path);
    out["ok"] = ok;
    if (!ok) {
      out["error"] = "failed to create instrument";
      return 1;
    }

    auto instruments = registry.list_instruments();
    if (!instruments.empty())
      out["instrument"] = instruments.back();

    return 0;
  } catch (const std::exception &e) {
    out["ok"] = false;
    out["error"] = std::string("exception: ") + e.what();
    return 1;
  }
}

int handle_stop(const json &params, json &out) {
  out = json::object();
  std::string name = params.value("name", "");
  if (name.empty()) {
    out["ok"] = false;
    out["error"] = "missing name";
    return 1;
  }

  auto &registry = InstrumentRegistry::instance();
  if (!registry.has_instrument(name)) {
    out["ok"] = false;
    out["error"] = "instrument not found";
    return 1;
  }

  registry.remove_instrument(name);
  out["ok"] = true;
  return 0;
}

int handle_status(const json &params, json &out) {
  out = json::object();
  std::string name = params.value("name", "");
  if (name.empty()) {
    out["ok"] = false;
    out["error"] = "missing name";
    return 1;
  }

  auto &registry = InstrumentRegistry::instance();
  auto proxy = registry.get_instrument(name);
  if (!proxy) {
    out["ok"] = false;
    out["error"] = "instrument not found";
    return 1;
  }

  out["ok"] = true;
  out["name"] = name;
  out["alive"] = proxy->is_alive();
  auto stats = proxy->get_stats();
  out["stats"] = {{"commands_sent", stats.commands_sent},
                  {"commands_completed", stats.commands_completed},
                  {"commands_failed", stats.commands_failed},
                  {"commands_timeout", stats.commands_timeout}};
  return 0;
}

int handle_list(const json &params, json &out) {
  (void)params;
  out = json::object();
  auto &registry = InstrumentRegistry::instance();
  auto instruments = registry.list_instruments();
  // RPC handler reutrn success even if the list is empty
  out["ok"] = true;
  out["instruments"] = instruments;
  return 0;
}

int handle_measure(const json &params, json &out) {
  out = json::object();
  std::string script_path = params.value("script_path", "");
  std::string log_level = params.value("log_level", "info");
  bool json_output = params.value("json", false);

  if (script_path.empty()) {
    out["ok"] = false;
    out["error"] = "missing script_path";
    return 1;
  }

  InstrumentLogger::instance().init("instrument_server.log",
                                    spdlog::level::from_str(log_level));

  try {
    auto &registry = InstrumentRegistry::instance();
    auto instruments = registry.list_instruments();
    if (instruments.empty()) {
      out["ok"] = false;
      out["error"] = "no instruments running";
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

    if (!json_output) {
      // If RPC caller requested non-json, we still return structured JSON
      LOG_INFO("SERVER", "MEASURE", "Running measurement (text mode)");
    }

    auto result = lua.safe_script_file(script_path);

    if (!result.valid()) {
      sol::error err = result;
      out["ok"] = false;
      out["error"] = err.what();
      return 1;
    }

    // Get collected results
    const auto &results = ctx.get_results();
    out["ok"] = true;
    out["script"] = std::filesystem::path(script_path).filename().string();
    out["results"] = json::array();

    for (size_t i = 0; i < results.size(); ++i) {
      const auto &r = results[i];
      json result_json;
      result_json["index"] = i;
      result_json["instrument"] = r.instrument_name;
      result_json["verb"] = r.verb;

      // Convert params to JSON
      json params_json;
      for (const auto &kv : r.params) {
        const auto &key = kv.first;
        const auto &value = kv.second;
        params_json[key] = [&value]() -> json {
          if (auto d = std::get_if<double>(&value))
            return *d;
          if (auto i = std::get_if<int64_t>(&value))
            return *i;
          if (auto s = std::get_if<std::string>(&value))
            return *s;
          if (auto b = std::get_if<bool>(&value))
            return *b;
          if (auto arr = std::get_if<std::vector<double>>(&value))
            return *arr;
          return nullptr;
        }();
      }
      result_json["params"] = params_json;

      // Execution timestamp (ms since epoch)
      auto ms_since_epoch =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              r.executed_at.time_since_epoch())
              .count();
      result_json["executed_at_ms"] = ms_since_epoch;

      // Return value
      json return_json;
      if (r.has_large_data) {
        return_json["type"] = "buffer";
        return_json["buffer_id"] = r.buffer_id;
        return_json["element_count"] = r.element_count;
        return_json["data_type"] = r.data_type;
      } else if (r.return_value) {
        return_json["type"] = r.return_type;
        if (auto d = std::get_if<double>(&*r.return_value))
          return_json["value"] = *d;
        else if (auto i = std::get_if<int64_t>(&*r.return_value))
          return_json["value"] = *i;
        else if (auto s = std::get_if<std::string>(&*r.return_value))
          return_json["value"] = *s;
        else if (auto b = std::get_if<bool>(&*r.return_value))
          return_json["value"] = *b;
      }
      result_json["return"] = return_json;

      out["results"].push_back(result_json);
    }

    return 0;
  } catch (const std::exception &e) {
    out["ok"] = false;
    out["error"] = std::string("exception: ") + e.what();
    return 1;
  }
}

int handle_test(const json &params, json &out) {
  out = json::object();
  std::string config_path = params.value("config_path", "");
  std::string verb = params.value("verb", "");
  std::string custom_plugin = params.value("plugin", "");
  std::string log_level = params.value("log_level", "info");
  json param_values = params.value("params", json::object());

  if (config_path.empty() || verb.empty()) {
    out["ok"] = false;
    out["error"] = "missing config_path or verb";
    return 1;
  }

  InstrumentLogger::instance().init("instrument_server.log",
                                    spdlog::level::from_str(log_level));

  try {
    // If a custom plugin path was provided, try to load it via PluginLoader.
    if (!custom_plugin.empty()) {
      if (!std::filesystem::exists(custom_plugin)) {
        out["ok"] = false;
        out["error"] = "plugin file not found";
        return 1;
      }

      plugin::PluginLoader loader(custom_plugin);
      if (!loader.is_loaded()) {
        out["ok"] = false;
        out["error"] = "failed to load plugin";
        return 1;
      }

      auto metadata = loader.get_metadata();
      plugin::PluginRegistry::instance().load_plugin(metadata.protocol_type,
                                                     custom_plugin);
    }

    auto &registry = InstrumentRegistry::instance();

    // Create instrument (this starts worker)
    bool created = registry.create_instrument(config_path);
    if (!created) {
      out["ok"] = false;
      out["error"] = "failed to create instrument";
      return 1;
    }

    auto instruments = registry.list_instruments();
    if (instruments.empty()) {
      out["ok"] = false;
      out["error"] = "no instrument created";
      return 1;
    }

    std::string instrument_name = instruments.back();
    auto proxy = registry.get_instrument(instrument_name);
    if (!proxy) {
      out["ok"] = false;
      out["error"] = "failed to get instrument proxy";
      registry.remove_instrument(instrument_name);
      return 1;
    }

    // Build SerializedCommand
    SerializedCommand cmd;
    cmd.id = "rpc-test-cmd";
    cmd.instrument_name = instrument_name;
    cmd.verb = verb;
    cmd.expects_response = true;

    // params is a JSON object of key->value; attempt to convert strings to
    // numbers where sensible
    for (auto it = param_values.begin(); it != param_values.end(); ++it) {
      if (it->is_number_integer()) {
        cmd.params[it.key()] = it->get<int64_t>();
      } else if (it->is_number_float()) {
        cmd.params[it.key()] = it->get<double>();
      } else if (it->is_boolean()) {
        cmd.params[it.key()] = it->get<bool>();
      } else if (it->is_string()) {
        cmd.params[it.key()] = it->get<std::string>();
      } else {
        // fallback to string
        cmd.params[it.key()] = it->dump();
      }
    }

    auto resp =
        proxy->execute_sync(std::move(cmd), std::chrono::milliseconds(5000));

    out["ok"] = resp.success;
    out["success"] = resp.success;
    out["error_message"] = resp.error_message;
    out["text_response"] = resp.text_response;
    if (resp.success && resp.return_value) {
      // return_value is std::variant - we will only serialize a few types
      if (std::holds_alternative<double>(resp.return_value.value()))
        out["return_value"] = std::get<double>(resp.return_value.value());
      else if (std::holds_alternative<int64_t>(resp.return_value.value()))
        out["return_value"] = std::get<int64_t>(resp.return_value.value());
      else if (std::holds_alternative<std::string>(resp.return_value.value()))
        out["return_value"] = std::get<std::string>(resp.return_value.value());
      else if (std::holds_alternative<bool>(resp.return_value.value()))
        out["return_value"] = std::get<bool>(resp.return_value.value());
    }

    // cleanup instrument
    registry.remove_instrument(instrument_name);

    return resp.success ? 0 : 1;
  } catch (const std::exception &e) {
    out["ok"] = false;
    out["error"] = std::string("exception: ") + e.what();
    return 1;
  }
}

int handle_discover(const json &params, json &out) {
  out = json::object();
  std::vector<std::string> search_paths;
  if (params.contains("paths") && params["paths"].is_array()) {
    for (auto &p : params["paths"])
      search_paths.push_back(p.get<std::string>());
  } else {
    search_paths = {"/usr/local/lib/instrument-plugins",
                    "/usr/lib/instrument-plugins", "./plugins", "."};
  }

  auto &plugin_registry = plugin::PluginRegistry::instance();
  // Ensure built-in plugins are loaded and standard discovery is performed.
  // Use call_once to avoid repeated loads across multiple handler calls.
  static std::once_flag g_plugins_init_flag;
  std::call_once(g_plugins_init_flag, [&]() {
    plugin_registry.load_builtin_plugins();
    plugin_registry.discover_plugins(search_paths);
  });

  // If we haven't discovered via call_once (e.g. because paths differ), still
  // run discover for the requested paths (idempotent).
  plugin_registry.discover_plugins(search_paths);

  auto protocols = plugin_registry.list_protocols();

  out["ok"] = true;
  out["protocols"] = protocols;
  out["paths"] = search_paths;
  return 0;
}

int handle_plugins(const json &params, json &out) {
  (void)params;
  out = json::object();
  auto &plugin_registry = plugin::PluginRegistry::instance();
  std::vector<std::string> plugin_paths = {"/usr/local/lib/instrument-plugins",
                                           "/usr/lib/instrument-plugins",
                                           "./plugins", "."};
  // Ensure built-in plugins are present before listing; idempotent.
  static std::once_flag g_plugins_init_flag2;
  std::call_once(g_plugins_init_flag2,
                 [&]() { plugin_registry.load_builtin_plugins(); });
  // Also run discover for the current invocation in case tests override paths.
  plugin_registry.discover_plugins(plugin_paths);

  auto protocols = plugin_registry.list_protocols();
  out["ok"] = true;
  out["plugins"] = json::array();
  for (const auto &protocol : protocols) {
    json p;
    p["protocol"] = protocol;
    p["path"] = plugin_registry.get_plugin_path(protocol);
    out["plugins"].push_back(p);
  }
  out["total"] = protocols.size();
  return 0;
}

// --- Job-related handlers ---

int handle_submit_job(const json &params, json &out) {
  out = json::object();
  std::string job_type = params.value("job_type", "");
  json job_params = params.value("params", json::object());
  if (job_type.empty()) {
    out["ok"] = false;
    out["error"] = "missing job_type";
    return 1;
  }
  auto &mgr = JobManager::instance();
  std::string jid = mgr.submit_job(job_type, job_params);
  out["ok"] = true;
  out["job_id"] = jid;
  return 0;
}

int handle_submit_measure(const json &params, json &out) {
  out = json::object();
  std::string script_path = params.value("script_path", "");
  if (script_path.empty()) {
    out["ok"] = false;
    out["error"] = "missing script_path";
    return 1;
  }
  json p = params;
  auto jid = JobManager::instance().submit_measure(script_path, p);
  out["ok"] = true;
  out["job_id"] = jid;
  return 0;
}

int handle_job_status(const json &params, json &out) {
  out = json::object();
  std::string jid = params.value("job_id", "");
  if (jid.empty()) {
    out["ok"] = false;
    out["error"] = "missing job_id";
    return 1;
  }
  JobInfo info;
  if (!JobManager::instance().get_job_info(jid, info)) {
    out["ok"] = false;
    out["error"] = "job not found";
    return 1;
  }
  out["ok"] = true;
  out["job_id"] = info.id;
  out["status"] = info.status;
  out["created_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                          info.created_at.time_since_epoch())
                          .count();
  if (info.status == "running" || info.status == "completed" ||
      info.status == "failed" || info.status == "canceled") {
    out["started_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                            info.started_at.time_since_epoch())
                            .count();
  }
  if (info.status == "completed" || info.status == "failed" ||
      info.status == "canceled") {
    out["finished_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                             info.finished_at.time_since_epoch())
                             .count();
  }
  return 0;
}

int handle_job_result(const json &params, json &out) {
  out = json::object();
  std::string jid = params.value("job_id", "");
  if (jid.empty()) {
    out["ok"] = false;
    out["error"] = "missing job_id";
    return 1;
  }
  json result;
  if (!JobManager::instance().get_job_result(jid, result)) {
    // Could be not finished or not found
    JobInfo info;
    if (!JobManager::instance().get_job_info(jid, info)) {
      out["ok"] = false;
      out["error"] = "job not found";
      return 1;
    }
    if (info.status != "completed") {
      out["ok"] = false;
      out["error"] = "job not completed";
      out["status"] = info.status;
      if (!info.error.empty())
        out["error_detail"] = info.error;
      return 1;
    }
    // otherwise missing result
    out["ok"] = false;
    out["error"] = "no result available";
    return 1;
  }
  out["ok"] = true;
  out["result"] = result;
  return 0;
}

int handle_job_list(const json &params, json &out) {
  (void)params;
  out = json::object();
  auto jobs = JobManager::instance().list_jobs();
  out["ok"] = true;
  out["jobs"] = json::array();
  for (const auto &j : jobs) {
    json ji;
    ji["job_id"] = j.id;
    ji["type"] = j.type;
    ji["status"] = j.status;
    ji["created_at"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                           j.created_at.time_since_epoch())
                           .count();
    out["jobs"].push_back(ji);
  }
  return 0;
}

int handle_job_cancel(const json &params, json &out) {
  out = json::object();
  std::string jid = params.value("job_id", "");
  if (jid.empty()) {
    out["ok"] = false;
    out["error"] = "missing job_id";
    return 1;
  }
  bool ok = JobManager::instance().cancel_job(jid);
  out["ok"] = ok;
  if (!ok)
    out["error"] = "failed to cancel job (maybe already finished)";
  return ok ? 0 : 1;
}
} // namespace server
} // namespace instserver
