#include "instrument-script-server/server/CommandHandlers.hpp"
#include "instrument-script-server/ipc/DataBufferManager.hpp"
#include "instrument-script-server/plugin/PluginLoader.hpp"
#include "instrument-script-server/plugin/PluginRegistry.hpp"
#include "instrument-script-server/server/InstrumentRegistry.hpp"
#include "instrument-script-server/server/JobManager.hpp"
#include "instrument-script-server/server/RuntimeContext.hpp"
#include "instrument-script-server/server/ServerDaemon.hpp"
#include <fmt/format.h>
#include <instrument-call-stack/instrument-call-stack-lua.h>
#include <instrument-data.h>
#include <instrument-log/inst_logging.h>

#include <fstream>
#include <instrument-plugin.h>
#include <sol/sol.hpp>
#include <string>
#include <vector>

using json = nlohmann::json;
namespace {
template <typename T> std::vector<T> make_vector(const void *data, size_t n) {
  const T *ptr = static_cast<const T *>(data);
  return std::vector<T>(ptr, ptr + n);
}
struct Json {
  std::map<std::string,
           std::variant<bool, size_t, std::string, std::vector<double>>>
      obj;

  auto &operator[](const std::string &key) { return obj[key]; }
};
// read entire file into a string for use with LUA library loading
std::string read_file_to_string(const std::filesystem::path &p) {
  std::ifstream ifs(p, std::ios::in | std::ios::binary);
  if (!ifs) {
    return "";
  }
  return {std::istreambuf_iterator<char>(ifs),
          std::istreambuf_iterator<char>()};
}
void preload_lua_modules_from_dir(sol::state &lua, const std::string &libroot) {
  namespace fs = std::filesystem;

  sol::table package = lua["package"];
  // optionally append libroot/?.lua to package.path so require still works on
  // disk
  std::string current_path = package["path"];
  std::string append = ";" + libroot + "/?.lua;" + libroot + "/?/init.lua";
  package["path"] = current_path + append;

  sol::table preload = package["preload"];

  // Walk libroot and preload .lua files. Convert e.g. lib/foo/bar.lua ->
  // "foo.bar"
  for (auto const &entry : fs::recursive_directory_iterator(libroot)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto &path = entry.path();
    if (path.extension() != ".lua") {
      continue;
    }
    // compute module name relative to libroot
    fs::path rel = fs::relative(path, libroot);
    // strip extension
    rel = rel.replace_extension("");
    // convert path separators to dots
    std::string module = rel.generic_string(); // uses '/' separators
    for (auto &c : module) {
      if (c == '/') {
        c = '.';
      }
    }
    // read file
    std::string code = read_file_to_string(path);
    if (code.empty()) {
      continue;
    }
    // load the module code as a chunk with module filename for meaningful
    // errors
    sol::load_result loader = lua.load(code, path.string());
    if (!loader.valid()) {
      sol::error err = loader;
      // log and skip problematic helper file
      LOG_ERROR("SERVER", "MEASURE", "Failed to load helper %s: %s",
                path.string().c_str(), err.what());
      continue;
    }
    // Convert to protected_function (stable registry reference) before
    // assigning to the table — sol::load_result holds a raw Lua stack index
    // and must not be stored directly in a table proxy.
    sol::protected_function fn = loader;
    preload[module] = fn;
    LOG_INFO("SERVER", "MEASURE", "Preloaded Lua module '%s'", module.c_str());
  }
}
void load_bundle_file(sol::state &lua, const std::string &bundle_path) {
  std::string code = read_file_to_string(bundle_path);
  if (code.empty()) {
    LOG_ERROR("SERVER", "MEASURE", "bundle file empty: %s",
              bundle_path.c_str());
    return;
  }
  sol::load_result loader = lua.load(code, bundle_path);
  if (!loader.valid()) {
    sol::error err = loader;
    LOG_ERROR("SERVER", "MEASURE", "failed to load bundle %s: %s",
              bundle_path.c_str(), err.what());
    return;
  }
  sol::protected_function_result pres = loader();
  if (!pres.valid()) {
    sol::error err = pres;
    LOG_ERROR("SERVER", "MEASURE", "bundle execution error %s: %s",
              bundle_path.c_str(), err.what());
    return;
  }
  // If the bundle returned a table, register it as a Lua global under the
  // file's stem name (e.g., "multimeter.lua" -> lua["multimeter"] = table).
  // This allows measurement scripts to access the bundle proxy via the stem.
  if (pres.return_count() > 0) {
    sol::object ret = pres[0];
    if (ret.get_type() == sol::type::table) {
      std::string stem = std::filesystem::path(bundle_path).stem().string();
      lua[stem] = ret;
      LOG_INFO("SERVER", "MEASURE", "Registered bundle global '%s'",
               stem.c_str());
    }
  }
  LOG_INFO("SERVER", "MEASURE", "Loaded bundle %s", bundle_path.c_str());
}
} // namespace

namespace instserver::server {

// The environment variable INSTRUMENT_SCRIPT_SERVER_RPC_PORT can be used to set
// this port from outside.
constexpr int DEFAULT_PORT = 8555;

sol::object json_to_lua(sol::state_view lua, const json &json_value) {
  switch (json_value.type()) {
  case json::value_t::null:
    return sol::make_object(lua, sol::nil);
  case json::value_t::boolean:
    return sol::make_object(lua, json_value.get<bool>());
  case json::value_t::number_integer:
    return sol::make_object(lua, json_value.get<int64_t>());
  case json::value_t::number_unsigned:
    return sol::make_object(lua, json_value.get<uint64_t>());
  case json::value_t::number_float:
    return sol::make_object(lua, json_value.get<double>());
  case json::value_t::string:
    return sol::make_object(lua, json_value.get<std::string>());
  case json::value_t::array: {
    sol::table t = lua.create_table();
    std::size_t idx = 1;
    for (const auto &elem : json_value) {
      t[idx++] = json_to_lua(lua, elem);
    }
    return sol::make_object(lua, t);
  }
  case json::value_t::object: {
    sol::table t = lua.create_table();
    for (auto it = json_value.begin(); it != json_value.end(); ++it) {
      t[it.key()] = json_to_lua(lua, it.value());
    }
    return sol::make_object(lua, t);
  }
  default:
    // fallback: stringify
    return sol::make_object(lua, json_value.dump());
  }
}

void load_optional_lua_libs(sol::state &lua) {
  const char *envp = std::getenv("INSTRUMENT_SCRIPT_SERVER_OPT_LUA_LIB");
  if (envp == nullptr) {
    LOG_INFO("SERVER", "MEASURE",
             "No external Lua helpers path set "
             "(INSTRUMENT_SCRIPT_SERVER_OPT_LUA_LIB)");
    return;
  }

  // Support multiple paths separated by semicolons
  std::string paths_str(envp);
  std::vector<std::string> paths;
  size_t start = 0;
  size_t end = paths_str.find(';');

  while (end != std::string::npos) {
    paths.push_back(paths_str.substr(start, end - start));
    start = end + 1;
    end = paths_str.find(';', start);
  }
  paths.push_back(paths_str.substr(start));

  // Load each path
  for (const auto &path_str : paths) {
    if (path_str.empty()) {
      continue;
    }

    std::filesystem::path p(path_str);
    if (!std::filesystem::exists(p)) {
      LOG_WARN("SERVER", "MEASURE", "Specified helpers path does not exist: %s",
               path_str.c_str());
      continue;
    }
    if (std::filesystem::is_directory(p)) {
      preload_lua_modules_from_dir(lua, p.string());
    } else if (std::filesystem::is_regular_file(p)) {
      // treat as bundle file
      load_bundle_file(lua, p.string());
    } else {
      LOG_WARN("SERVER", "MEASURE", "Helpers path not dir or file: %s",
               path_str.c_str());
    }
  }
}
/*
  Each handler expects a params JSON object with keys as described below
  and fills `out` with a JSON response containing at minimum {"ok":
  true|false}
*/

int handle_daemon(const json &params, json &out) {
  out = json::object();
  std::string action = params.value("action", "");

  auto &daemon = ServerDaemon::instance();

  if (action == "start") {
    int pid = -1;

#ifdef _WIN32
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);

    std::string cmd = std::string(exe_path) + " daemon run";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    BOOL ok = CreateProcessA(NULL, cmd.data(), NULL, NULL, FALSE,
                             DETACHED_PROCESS | CREATE_NO_WINDOW, NULL, NULL,
                             &si, &pi);

    if (!ok) {
      out["ok"] = false;
      out["error"] = "Failed to launch daemon process";
      return 1;
    }

    // ✅ Get PID directly on Windows
    pid = static_cast<int>(pi.dwProcessId);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

#else
    pid_t child_pid = fork();

    if (child_pid < 0) {
      out["ok"] = false;
      out["error"] = "fork failed";
      return 1;
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

      char exe_path[1024];
      ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
      if (len > 0) {
        exe_path[len] = '\0';
        execl(exe_path, exe_path, "daemon", "run", nullptr);
      }

      _exit(1); // exec failed
    }

    // ✅ Parent already knows PID
    pid = static_cast<int>(child_pid);
#endif

    // ✅ Optional: wait for daemon to write PID file (ensures it's fully
    // started)
    for (int i = 0; i < 20; ++i) {
      int file_pid = ServerDaemon::get_daemon_pid();
      if (file_pid > 0) {
        pid = file_pid;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (pid <= 0) {
      out["ok"] = false;
      out["error"] = "daemon not started";
      return 1;
    }

    out["pid"] = pid;
    out["ok"] = true;
    out["message"] = "daemon started";
    return 0;
  }

  if (action == "run") {
    // ---- Logging setup ----
    std::string log_level = params.value("log_level", "info");
    uint8_t level = INST_LOG_INFO;

    if (log_level == "trace")
      level = INST_LOG_TRACE;
    else if (log_level == "debug")
      level = INST_LOG_DEBUG;
    else if (log_level == "warn")
      level = INST_LOG_WARN;
    else if (log_level == "error")
      level = INST_LOG_ERROR;

    inst_log_init("instrument_server.log", level, "instrument",
                  10 * 1024 * 1024, 3);

    LOG_INFO("DAEMON", "RUN", "Entered daemon run mode");

    // ---- RPC port config ----
    const char *rpc_port_env = std::getenv("INSTRUMENT_SCRIPT_SERVER_RPC_PORT");

    if (rpc_port_env && rpc_port_env[0] != 0) {
      try {
        int port = std::stoi(rpc_port_env);
        if (port > 0 && port <= 65535) {
          daemon.set_rpc_port(static_cast<uint16_t>(port));
        } else {
          daemon.set_rpc_port(DEFAULT_PORT);
        }
      } catch (...) {
        daemon.set_rpc_port(DEFAULT_PORT);
      }
    }

    bool ok = daemon.start();
    LOG_INFO("DAEMON", "RUN", "daemon.start() returned %d", ok);

    if (!ok) {
      out["ok"] = false;
      out["error"] = "daemon start failed";
      return 1;
    }

    while (daemon.is_running()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    daemon.stop();
    return 0;
  }

  if (action == "stop") {
    if (!ServerDaemon::is_already_running()) {
      out["ok"] = true;
      out["message"] = "daemon not running";
      return 0;
    }

    daemon.stop();

    out["ok"] = true;
    out["message"] = "daemon stopped";
    return 0;
  }

  if (action == "status") {
    bool running = ServerDaemon::is_already_running();

    out["ok"] = true;
    out["running"] = running;

    if (running) {
      int pid = ServerDaemon::get_daemon_pid();
      out["pid"] = pid;
      out["message"] = fmt::format("daemon running (pid={})", pid);
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
    if (!instruments.empty()) {
      out["instrument"] = instruments.back();
    }

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

sol::object callstack_from_serialized(sol::state &lua,
                                      const std::string &serialized) {
  lua_State *L = lua.lua_state();

  // Deserialize
  CallStack *stack = instrument_call_stack_deserialize(serialized.c_str());
  if (stack == nullptr) {
    throw std::runtime_error("Failed to deserialize CallStack");
  }

  // Push userdata (Lua now owns it → GC will free it)
  push_callstack(L, stack, /*owned=*/1);

  // Convert stack top to sol::object
  sol::object obj = sol::stack::get<sol::object>(L, -1);

  // Pop from Lua stack (important!)
  lua_pop(L, 1);

  return obj;
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

  try {
    auto &registry = InstrumentRegistry::instance();
    auto instruments = registry.list_instruments();
    if (instruments.empty()) {
      out["ok"] = false;
      out["error"] = "no instruments running";
      return 1;
    }

    LOG_INFO("SERVER", "MEASURE", "Script: %s", script_path.c_str());

    // Setup Lua
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                       sol::lib::string, sol::lib::io, sol::lib::os,
                       sol::lib::package);
    load_optional_lua_libs(lua);
    register_instrument_call_stack(lua.lua_state());

    auto &sync = ServerDaemon::instance().sync_coordinator();
    bind_runtime_context(lua, registry, sync);

    // Create default context (host-side C++ runtime object)
    auto ctx_shared = std::make_shared<RuntimeContext>(
        registry, ServerDaemon::instance().sync_coordinator());
    lua["context"] =
        ctx_shared; // keep the existing behavior to bind the userdata

    // If the RPC payload included a context_spec, inject it (in-memory)
    // before running the script
    if (params.contains("globals")) {
      // convert JSON to a Lua table
      sol::object spec_obj = json_to_lua(lua, params["globals"]);
      lua["__spec"] = spec_obj; // place it in the Lua state temporarily

      // pass block_inject_globals flag (default false)
      bool block_inject_globals = params.value("block_inject_globals", false);
      lua["__block_inject_globals"] = block_inject_globals;

      // Optional: pass schema version for server-side logic if needed
      if (params.contains("context_schema_version")) {
        lua["__context_schema_version"] =
            params["context_schema_version"].get<std::string>();
      } else {
        lua["__context_schema_version"] = sol::nil;
      }

      // Merge snippet executed inside the Lua VM (keeps host methods and
      // creates instrument target wrappers)
      const char *merge_snippet = R"lua(
-- Merge injected __spec into host-provided context (do not overwrite host methods).
-- __spec is a Lua table created from JSON by the C++ host.

-- Reserved context method names (must match RuntimeContext usertype definition)
local RESERVED_CONTEXT_METHODS = {
  call = true,
  parallel = true,
  log = true,
  error = true
}

local function is_primitive(v)
  local t = type(v)
  return t == "number" or t == "string" or t == "boolean" or t == "nil" or t == "function" or t == "userdata"
end

local function make_readonly(val, seen)
  if is_primitive(val) then return val end
  if type(val) ~= "table" then return val end
  seen = seen or setmetatable({}, { __mode = "k" })
  if seen[val] then return seen[val] end
  local proxy = {}
  seen[val] = proxy
  local mt = {
    __index = function(_, k) return make_readonly(val[k], seen) end,
    __newindex = function(_, k, v) error(("attempt to modify read-only context field '%s'"):format(tostring(k)), 2) end,
    __pairs = function(_) local function iter(t, k) local nk, nv = next(t, k) if nk==nil then return nil end return nk, make_readonly(nv, seen) end return iter, val, nil end,
    __ipairs = function(_) local i=0 local function iter() i=i+1 local vv=val[i] if vv==nil then return nil end return i, make_readonly(vv, seen) end return iter end,
    __metatable = "read-only-context-proxy",
  }
  setmetatable(proxy, mt)
  return proxy
end

-- Store injected variable names for logging
__injected_vars = {}

if not __block_inject_globals then
  for k, v in pairs(__spec or {}) do
    if not RESERVED_CONTEXT_METHODS[k] then
      if type(v) == "table" then
        _G[k] = make_readonly(v)
      else
        _G[k] = v
      end
      table.insert(__injected_vars, k)
    end
  end
end

-- Clean up temporary globals to avoid leaking
__spec = nil
__block_inject_globals = nil
__context_schema_version = nil
)lua";

      // run the merge snippet
      sol::protected_function_result merge_result =
          lua.safe_script(merge_snippet, &sol::script_pass_on_error);
      if (!merge_result.valid()) {
        sol::error err = merge_result;
        out["ok"] = false;
        out["error"] =
            std::string("context_spec injection error: ") + err.what();
        return 1;
      }

      // Log warnings for each injected global variable
      sol::optional<sol::table> injected_vars = lua["__injected_vars"];
      if (injected_vars) {
        for (size_t i = 1; i <= injected_vars->size(); ++i) {
          sol::optional<std::string> var_name = (*injected_vars)[i];
          if (var_name) {
            LOG_WARN("SERVER", "MEASURE",
                     "Injecting global variable '%s' from spec",
                     var_name->c_str());
          }
        }
      }
      lua["__injected_vars"] = sol::nil; // Clean up
    } // end if contains context_spec

    if (!json_output) {
      // If RPC caller requested non-json, we still return structured JSON
      LOG_INFO("SERVER", "MEASURE", "Running measurement (text mode)");
    }

    // Load the script file
    auto load_result = lua.safe_script_file(script_path);

    if (!load_result.valid()) {
      sol::error err = load_result;
      out["ok"] = false;
      out["error"] = std::string("Script load error: ") + err.what();
      return 1;
    }

    // Check if the script defined a main function (new format).
    // Support both styles:
    //   (a) global: main = fn
    //   (b) return table: return { main = fn }
    sol::optional<sol::protected_function> main_func = lua["main"];
    if (!main_func && load_result.valid()) {
      sol::object ret_val = load_result;
      if (ret_val.get_type() == sol::type::table) {
        main_func = ret_val.as<sol::table>()["main"];
      }
    }

    if (main_func) {
      // New format: call main function with context
      LOG_INFO("SERVER", "MEASURE",
               "Executing script with main function (new format)");

      // Check if type_manifest is provided (Teal static typing support)
      if (params.contains("type_manifest")) {
        const auto &manifest = params["type_manifest"];

        // Validate manifest structure
        if (!manifest.contains("parameters") ||
            !manifest["parameters"].is_array()) {
          out["ok"] = false;
          out["error"] =
              "Invalid type_manifest: missing or invalid 'parameters' array";
          return 1;
        }

        // Build arguments based on manifest
        std::vector<sol::object> args;
        args.push_back(sol::make_object(
            lua, ctx_shared.get())); // First arg is always context

        const auto &param_defs = manifest["parameters"];
        for (size_t i = 1; i < param_defs.size(); ++i) { // Skip first (context)
          const auto &param = param_defs[i];

          if (!param.contains("name") || !param["name"].is_string()) {
            out["ok"] = false;
            out["error"] = fmt::format(
                "Invalid type_manifest: parameter {} missing 'name'", i);
            return 1;
          }

          std::string param_name = param["name"];

          // Check if this parameter exists in globals
          if (!params.contains("globals") ||
              !params["globals"].contains(param_name)) {
            out["ok"] = false;
            out["error"] =
                fmt::format("Missing required parameter '{}' (declared in "
                            "type_manifest but not provided in globals)",
                            param_name);
            LOG_ERROR("SERVER", "MEASURE",
                      "Missing required parameter '%s' for typed main function",
                      param_name.c_str());
            return 1;
          }

          // Convert JSON value to Lua object
          const auto &value = params["globals"][param_name];
          std::string type = param.value("type", "");

          sol::object arg;

          if (type == "CallStack") {
            // Validate input
            if (!value.is_string()) {
              out["ok"] = false;
              out["error"] = "CallStack must be a serialized string";
              return 1;
            }

            try {
              arg = callstack_from_serialized(lua, value.get<std::string>());
            } catch (const std::exception &e) {
              out["ok"] = false;
              out["error"] =
                  std::string("CallStack deserialization failed: ") + e.what();
              return 1;
            }

          } else {
            arg = json_to_lua(lua, value);
          }
          args.push_back(arg);

          LOG_INFO("SERVER", "MEASURE",
                   "Passing parameter '%s' to main function (type: %s)",
                   param_name.c_str(), param.value("type", "unknown").c_str());
        }

        // Check for unused globals (warnings)
        if (params.contains("globals")) {
          for (auto it = params["globals"].begin();
               it != params["globals"].end(); ++it) {
            std::string global_name = it.key();
            bool found = false;

            for (size_t i = 1; i < param_defs.size(); ++i) {
              if (param_defs[i]["name"] == global_name) {
                found = true;
                break;
              }
            }

            if (!found) {
              LOG_WARN("SERVER", "MEASURE",
                       "Global variable '%s' provided but not used by typed "
                       "main function (injecting as global)",
                       global_name.c_str());
              // Still inject it as global for backward compatibility
              lua[global_name] = json_to_lua(lua, it.value());
            }
          }
        }

        // Call main with unpacked arguments
        sol::protected_function_result main_result =
            (*main_func)(sol::as_args(args));

        if (!main_result.valid()) {
          sol::error err = main_result;
          std::string error_msg =
              std::string("Script execution error: ") + err.what();
          if (ctx_shared->has_error()) {
            error_msg =
                ctx_shared->get_error() + " (Runtime: " + err.what() + ")";
          }
          out["ok"] = false;
          out["error"] = error_msg;
          return 1;
        }
      } else {
        // Legacy: call main with just context parameter
        sol::protected_function_result main_result =
            (*main_func)(ctx_shared.get());

        if (!main_result.valid()) {
          sol::error err = main_result;
          std::string error_msg =
              std::string("Script execution error: ") + err.what();
          // If context:error() was also called, include both messages
          if (ctx_shared->has_error()) {
            error_msg =
                ctx_shared->get_error() + " (Runtime: " + err.what() + ")";
          }
          out["ok"] = false;
          out["error"] = error_msg;
          return 1;
        }
      }

      // The main function should return results (optional)
      // We still collect results from context:call() operations
    } else {
      // Old format: script executed at load time (backward compatibility)
      LOG_WARN("SERVER", "MEASURE",
               "DEPRECATED: Script uses compatibility mode (no main function). "
               "Please migrate to new format with main(ctx) function.");
      out["error"] =
          "DEPRECATED: Compatibility mode will be removed in a future version";
      // Result was already executed during safe_script_file
    }

    // Get collected results
    const auto &results = ctx_shared->get_results();
    LOG_INFO("SERVER", "MEASURE",
             "Lua script done. Serializing %d results into HTTP response",
             results.size());
    out["ok"] = !ctx_shared->has_error();
    out["script"] = std::filesystem::path(script_path).filename().string();
    out["results"] = json::array();

    for (size_t i = 0; i < results.size(); ++i) {
      const auto &r = results[i];
      json result_json;
      result_json["index"] = i;
      result_json["instrument"] =
          instrument_call_stack_get_instrument_name(r.target.get());
      result_json["verb"] = instrument_call_stack_get_command(r.target.get());

      // Convert params to JSON
      json params_json;
      for (const auto &p : r.params) {
        const auto &key = p.name;
        switch (p.type) {
        case PARAM_TYPE_DOUBLE:
          params_json[key] = p.value.d_val;
          break;
        case PARAM_TYPE_INT64:
          params_json[key] = p.value.i64_val;
          break;
        case PARAM_TYPE_STRING:
          params_json[key] = p.value.str_val;
          break;
        case PARAM_TYPE_BOOL:
          params_json[key] = p.value.b_val;
          break;
        case PARAM_TYPE_BUFFER:
          params_json[key] = p.value.str_val;
          break;
        default:
          params_json[key] = nullptr;
          break;
        }
      }

      result_json["params"] = params_json;
      LOG_INFO("SERVER", "MEASURE", "Finished params for result %d", i);

      // Execution timestamp (ms since epoch)
      auto ms_since_epoch =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              r.executed_at.time_since_epoch())
              .count();
      result_json["executed_at_ms"] = ms_since_epoch;

      // Return value
      json return_json;
      if (r.returns.size() == 1) {
        const auto &v = r.returns[0];
        switch (v.type) {
        case PARAM_TYPE_DOUBLE:
          return_json["value"] = v.value.d_val;
          return_json["type"] = "float";
          break;
        case PARAM_TYPE_INT64:
          return_json["value"] = v.value.i64_val;
          return_json["type"] = "integer";
          break;
        case PARAM_TYPE_STRING:
          return_json["value"] = v.value.str_val;
          return_json["type"] = "string";
          break;
        case PARAM_TYPE_BOOL:
          return_json["value"] = v.value.b_val;
          return_json["type"] = "boolean";
          break;
        case PARAM_TYPE_BUFFER: {
          return_json["type"] = "buffer";
          return_json["buffer_id"] = v.value.str_val;
          return_json["value"] = v.value.str_val;

          auto out =
              ipc::DataBufferManager::instance().get_metadata(v.value.str_val);

          if (out) {
            return_json["element_count"] = out->element_count;

            switch (out->data_type) {
            case INST_DATA_FLOAT32:
              return_json["data_type"] = "float32";
              break;
            case INST_DATA_FLOAT64:
              return_json["data_type"] = "float64";
              break;
            case INST_DATA_INT32:
              return_json["data_type"] = "int32";
              break;
            case INST_DATA_INT64:
              return_json["data_type"] = "int64";
              break;
            case INST_DATA_UINT32:
              return_json["data_type"] = "uint32";
              break;
            case INST_DATA_UINT64:
              return_json["data_type"] = "uint64";
              break;
            case INST_DATA_UINT8:
              return_json["data_type"] = "uint8";
              break;
            default:
              return_json["data_type"] = "unknown";
              break;
            }
          }
          break;
        }
        default:
          return_json["type"] = "void";
          break;
        }
      } else {
        for (const auto &v : r.returns) {
          const auto &key = v.name;
          switch (v.type) {
          case PARAM_TYPE_DOUBLE:
            return_json[key]["value"] = v.value.d_val;
            return_json[key]["type"] = "float";
            break;
          case PARAM_TYPE_INT64:
            return_json[key]["value"] = v.value.i64_val;
            return_json[key]["type"] = "integer";
            break;
          case PARAM_TYPE_STRING:
            return_json[key]["value"] = v.value.str_val;
            return_json[key]["type"] = "string";
            break;
          case PARAM_TYPE_BOOL:
            return_json[key]["value"] = v.value.b_val;
            return_json[key]["type"] = "boolean";
            break;
          case PARAM_TYPE_BUFFER: {
            return_json[key]["type"] = "buffer";
            return_json[key]["value"] = v.value.str_val;
            return_json[key]["buffer_id"] = v.value.str_val;

            auto out = ipc::DataBufferManager::instance().get_metadata(
                v.value.str_val);

            if (out) {
              return_json[key]["element_count"] = out->element_count;

              switch (out->data_type) {
              case INST_DATA_FLOAT32:
                return_json[key]["data_type"] = "float32";
                break;
              case INST_DATA_FLOAT64:
                return_json[key]["data_type"] = "float64";
                break;
              case INST_DATA_INT32:
                return_json[key]["data_type"] = "int32";
                break;
              case INST_DATA_INT64:
                return_json[key]["data_type"] = "int64";
                break;
              case INST_DATA_UINT32:
                return_json[key]["data_type"] = "uint32";
                break;
              case INST_DATA_UINT64:
                return_json[key]["data_type"] = "uint64";
                break;
              case INST_DATA_UINT8:
                return_json[key]["data_type"] = "uint8";
                break;
              default:
                return_json[key]["data_type"] = "unknown";
                break;
              }
            }
            break;
          }
          default:
            return_json[key]["type"] = "void";
            break;
          }
        }
      }
      result_json["return"] = return_json;

      out["results"].push_back(result_json);
    }
    LOG_INFO("SERVER", "MEASURE",
             "Serialization complete. Returning to HTTP handler to send "
             "response");
    if (ctx_shared->has_error()) {
      out["error"] = ctx_shared->get_error();
      return 1;
    }
    return 0;
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
    for (const auto &p : params["paths"]) {
      search_paths.push_back(p.get<std::string>());
    }
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

  // If we haven't discovered via call_once (e.g. because paths differ),
  // still run discover for the requested paths (idempotent).
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
  // Also run discover for the current invocation in case tests override
  // paths.
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
      if (!info.error.empty()) {
        out["error_detail"] = info.error;
      }
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
  if (!ok) {
    out["error"] = "failed to cancel job (maybe already finished)";
  }
  return ok ? 0 : 1;
}

int handle_list_buffers(const json &params, json &out) {
  (void)params;
  out = json::object();
  auto &mgr = ipc::DataBufferManager::instance();
  auto buffers = mgr.list_buffers();
  out["ok"] = true;
  out["buffers"] = json::array();
  for (const auto &id : buffers) {
    if (auto meta_opt = mgr.get_metadata(id)) {
      auto meta = *meta_opt;
      json b;
      b["buffer_id"] = id;
      b["element_count"] = meta.element_count;
      b["byte_size"] = meta.byte_size;
      b["data_type"] = meta.data_type;
      out["buffers"].push_back(b);
    }
  }
  return 0;
}

int handle_release_buffer(const json &params, json &out) {
  out = json::object();
  std::string id = params.value("buffer_id", "");
  if (id.empty()) {
    out["ok"] = false;
    out["error"] = "missing buffer_id";
    return 1;
  }
  auto &mgr = ipc::DataBufferManager::instance();
  mgr.release_buffer(id);
  out["ok"] = true;
  out["message"] = "Buffer released successfully";
  return 0;
}

int handle_get_buffer_metadata(const json &params, json &out) {
  out = json::object();
  std::string id = params.value("buffer_id", "");
  if (id.empty()) {
    out["ok"] = false;
    out["error"] = "missing buffer_id";
    return 1;
  }
  auto &mgr = ipc::DataBufferManager::instance();
  auto meta_opt = mgr.get_metadata(id);
  if (!meta_opt) {
    out["ok"] = false;
    out["error"] = "buffer not found";
    return 1;
  }
  auto meta = *meta_opt;
  out["ok"] = true;
  out["buffer_id"] = id;
  out["element_count"] = meta.element_count;
  out["byte_size"] = meta.byte_size;
  out["data_type"] = meta.data_type;
  return 0;
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

  if (meta.data_type == INST_DATA_FLOAT64) {
    out["data"] = make_vector<double>(data, n);
    out["data_type"] = INST_DATA_FLOAT64;

  } else if (meta.data_type == INST_DATA_FLOAT32) {
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
} // namespace instserver::server
