#include "instrument-script-server/server/CommandHandlers.hpp"
#include "instrument-script-server/ErrorCodes.hpp"
#include "instrument-script-server/ipc/DataBufferManager.hpp"
#include "instrument-script-server/plugin/PluginLoader.hpp"
#include "instrument-script-server/plugin/PluginRegistry.hpp"
#include "instrument-script-server/server/InstrumentRegistry.hpp"
#include "instrument-script-server/server/JobManager.hpp"
#include "instrument-script-server/server/RuntimeContext.hpp"
#include "instrument-script-server/server/ServerDaemon.hpp"
#include "instserver/server/v1/daemon_messages.pb.h"
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
  std::string current_path = package["path"];
  std::string append = ";" + libroot + "/?.lua;" + libroot + "/?/init.lua";
  package["path"] = current_path + append;

  sol::table preload = package["preload"];

  std::error_code ec;

  for (fs::recursive_directory_iterator it(libroot, ec), end; it != end;
       it.increment(ec)) {

    if (ec) {
      LOG_WARN("SERVER", "MEASURE", "Directory iteration error: %s",
               ec.message().c_str());
      break;
    }

    const auto &entry = *it;

    if (!entry.is_regular_file(ec)) {
      continue;
    }

    const auto &path = entry.path();

    if (path.extension() != ".lua") {
      continue;
    }

    fs::path rel = fs::relative(path, libroot, ec);
    if (ec) {
      continue;
    }

    rel.replace_extension("");

    std::string module = rel.generic_string();
    for (auto &c : module) {
      if (c == '/') {
        c = '.';
      }
    }

    std::string code = read_file_to_string(path);
    if (code.empty()) {
      continue;
    }

    sol::load_result loader = lua.load(code, path.string());
    if (!loader.valid()) {
      sol::error err = loader;
      LOG_ERROR("SERVER", "MEASURE", "Failed to load %s: %s",
                path.string().c_str(), err.what());
      continue;
    }

    sol::protected_function fn = loader;
    preload[module] = fn;
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
template <typename Range>
sol::object array_to_lua(sol::state_view lua, const Range &range) {
  sol::table t = lua.create_table();
  int idx = 1;
  for (const auto &v : range) {
    t[idx++] = v;
  }
  return sol::make_object(lua, t);
}
} // namespace

namespace instserver::server {

// The environment variable INSTRUMENT_SCRIPT_SERVER_RPC_PORT can be used to set
// this port from outside.
constexpr int DEFAULT_PORT = 8555;
sol::object variable_to_lua(sol::state_view lua, const v1::VariableValue *var) {

  switch (var->value_case()) {

  case v1::VariableValue::kIsNil:
    return sol::make_object(lua, sol::nil);

  case v1::VariableValue::kI:
    return sol::make_object(lua, var->i());

  case v1::VariableValue::kD:
    return sol::make_object(lua, var->d());

  case v1::VariableValue::kB:
    return sol::make_object(lua, var->b());

  case v1::VariableValue::kS:
    return sol::make_object(lua, var->s());

  case v1::VariableValue::kIArray:
    return array_to_lua(lua, var->i_array().values());

  case v1::VariableValue::kDArray:
    return array_to_lua(lua, var->d_array().values());

  case v1::VariableValue::kBArray:
    return array_to_lua(lua, var->b_array().values());

  case v1::VariableValue::kSArray:
    return array_to_lua(lua, var->s_array().values());

  case v1::VariableValue::kDbArray:
    return array_to_lua(lua, var->db_array().values());

  case v1::VariableValue::kCsArray:
    return array_to_lua(lua, var->cs_array().values());

  case v1::VariableValue::kMArray:
    return array_to_lua(lua, var->m_array().values());

  case v1::VariableValue::kMMap: {
    sol::table t = lua.create_table();
    for (const auto &[key, val] : var->m_map().values()) {
      t[key] = variable_to_lua(lua, &val);
    }
    return sol::make_object(lua, t);
  }

  case v1::VariableValue::VALUE_NOT_SET:
  default:
    return sol::make_object(lua, sol::nil);
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
int handle_daemon_status(const DaemonStatusRequest & /*req*/,
                         DaemonStatusResponse *resp) {
  bool running = ServerDaemon::is_already_running();

  auto *stdrp = resp->mutable_standard_response();
  stdrp->set_ok(true);
  resp->set_running(running);

  if (running) {
    int pid = ServerDaemon::get_daemon_pid();
    resp->set_pid(pid);
    stdrp->set_message(fmt::format("daemon running (pid={})", pid));
  } else {
    stdrp->set_message("daemon not running");
  }

  return 0;
}

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

    if (log_level == "trace") {
      level = INST_LOG_TRACE;
    } else if (log_level == "debug") {
      level = INST_LOG_DEBUG;
    } else if (log_level == "warn") {
      level = INST_LOG_WARN;
    } else if (log_level == "error") {
      level = INST_LOG_ERROR;
    }

    inst_log_init("instrument_server.log", level, "instrument",
                  10 * 1024 * 1024, 3);

    LOG_INFO("DAEMON", "RUN", "Entered daemon run mode");

    // ---- RPC port config ----
    const char *rpc_port_env = std::getenv("INSTRUMENT_SCRIPT_SERVER_RPC_PORT");

    if ((rpc_port_env != nullptr) && rpc_port_env[0] != 0) {
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

  out["ok"] = false;
  out["error"] = "Unknown daemon action";
  return 1;
}

int handle_start_instrument(const StartInstrumentRequest &req,
                            StartInstrumentResponse *resp) {

  const std::string &config_path = req.config_path();
  std::string plugin_path;
  if (req.has_plugin_path()) {
    plugin_path = req.plugin_path();
  }
  auto *stdrp = resp->mutable_standard_response();
  auto *err = stdrp->mutable_error();

  if (config_path.empty()) {
    stdrp->set_ok(false);
    auto *err = stdrp->mutable_error();
    err->set_message("missing config_path");
    err->set_code(ErrorCode::ERROR_CODE_INVALID_ARGUMENT);
    return 1;
  }

  try {
    if (!plugin_path.empty()) {
      if (!std::filesystem::exists(plugin_path)) {
        stdrp->set_ok(false);
        err->set_message("plugin file not found");
        err->set_code(ErrorCode::ERROR_CODE_FILE_DOES_NOT_EXIST);
        return 1;
      }

      plugin::PluginLoader loader(plugin_path);
      if (!loader.is_loaded()) {
        stdrp->set_ok(false);
        err->set_message("failed to load plugin");
        err->set_code(ErrorCode::ERROR_CODE_PLUGIN_CRASH);
        return 1;
      }

      auto metadata = loader.get_metadata();
      plugin::PluginRegistry::instance().load_plugin(metadata.protocol_type,
                                                     plugin_path);
    }

    auto &registry = InstrumentRegistry::instance();
    bool ok = registry.create_instrument(config_path);
    stdrp->set_ok(ok);
    if (!ok) {
      err->set_message("failed to create instrument");
      err->set_code(ErrorCode::ERROR_CODE_INSTRUMENT_CRASH);
      return 1;
    }

    return 0;
  } catch (const std::exception &e) {
    stdrp->set_ok(false);
    err->set_message(std::string("exception: ") + e.what());
    err->set_code(ErrorCode::ERROR_CODE_RUNTIME);
    return 1;
  }
}

int handle_stop_instrument(const StopInstrumentRequest &req,
                           StopInstrumentResponse *resp) {

  const std::string &name = req.instrument_name();
  auto *stdrp = resp->mutable_standard_response();
  auto *err = stdrp->mutable_error();
  if (name.empty()) {
    stdrp->set_ok(false);
    err->set_message("missing instrument_name");
    err->set_code(ErrorCode::ERROR_CODE_INVALID_ARGUMENT);
    return 1;
  }

  auto &registry = InstrumentRegistry::instance();
  if (!registry.has_instrument(name)) {
    stdrp->set_ok(false);
    err->set_message("instrument not found");
    err->set_code(ErrorCode::ERROR_CODE_RUNTIME);
    return 1;
  }

  registry.remove_instrument(name);
  stdrp->set_ok(true);
  return 0;
}

int handle_instrument_status(const InstrumentStatusRequest &req,
                             InstrumentStatusResponse *resp) {
  const std::string &name = req.instrument_name();
  auto *stdrp = resp->mutable_standard_response();
  auto *err = stdrp->mutable_error();
  if (name.empty()) {
    stdrp->set_ok(false);
    err->set_message("missing instrument_name");
    err->set_code(ErrorCode::ERROR_CODE_INVALID_ARGUMENT);
    return 1;
  }

  auto &registry = InstrumentRegistry::instance();
  auto proxy = registry.get_instrument(name);
  if (!proxy) {
    stdrp->set_ok(false);
    err->set_message("instrument not found");
    err->set_code(ErrorCode::ERROR_CODE_RUNTIME);
    return 1;
  }

  stdrp->set_ok(true);
  *resp->mutable_stats() = proxy->get_stats();
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

int handle_list_instruments(const ListInstrumentsRequest & /*req*/,
                            ListInstrumentsResponse *resp) {
  auto &registry = InstrumentRegistry::instance();
  auto instruments = registry.list_instruments();
  auto *stdrp = resp->mutable_standard_response();
  stdrp->set_ok(true);
  for (const auto &name : instruments) {
    resp->add_instrument_name(name);
  }
  return 0;
}

int handle_measure(const MeasureJobRequest &req,
                   MeasureJobResultResponse *resp) {
  const std::string &script_path = req.script_path();
  auto *stdrp = resp->mutable_standard_response();
  auto *err = stdrp->mutable_error();
  if (script_path.empty()) {
    stdrp->set_ok(false);
    err->set_message("Missing script_path");
    err->set_code(v1::ERROR_CODE_INVALID_ARGUMENT);
    return 1;
  }
  LOG_INFO("SERVER", "MEASURE", "Script: %s", script_path.c_str());

  auto &registry = InstrumentRegistry::instance();
  auto instruments = registry.list_instruments();
  if (instruments.empty()) {
    stdrp->set_ok(false);
    err->set_message("No instruments are running");
    err->set_code(v1::ERROR_CODE_RUNTIME);
    return 1;
  }
  auto &sync = ServerDaemon::instance().sync_coordinator();

  sol::state lua;
  try {
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table,
                       sol::lib::string, sol::lib::io, sol::lib::os,
                       sol::lib::package);
    load_optional_lua_libs(lua);
    register_instrument_call_stack(lua.lua_state());
    bind_runtime_context(lua, registry, sync);

    // Create default context (host-side C++ runtime object)
    auto ctx_shared = std::make_shared<RuntimeContext>(
        registry, ServerDaemon::instance().sync_coordinator());
    lua["context"] =
        ctx_shared; // keep the existing behavior to bind the userdata

    // Load the script file
    auto load_result = lua.safe_script_file(script_path);

    if (!load_result.valid()) {
      sol::error result = load_result;
      stdrp->set_ok(false);
      err->set_message(std::string("Script load error: ") + result.what());
      err->set_code(v1::ERROR_CODE_RUNTIME);
      return 1;
    }

    // Check if the script defined a main function (new format)
    sol::optional<sol::protected_function> main_func = lua["main"];
    if (!main_func.has_value()) {
      std::string error = "Missing main(ctx, ...) function.";
      LOG_ERROR("SERVER", "MEASURE", error.c_str());
      throw std::runtime_error(error);
    }

    LOG_INFO("SERVER", "MEASURE", "Executing script with main function");

    // Check if type_manifest is provided (Teal static typing support)
    if (req.has_type_manifest()) {
      const auto &manifest = req.type_manifest();

      // Build arguments based on manifest
      std::vector<sol::object> args;
      args.push_back(sol::make_object(
          lua, ctx_shared.get())); // First arg is always context

      const auto &param_defs = manifest.parameters();
      for (int i = 1; i < param_defs.size(); ++i) { // Skip first (context)
        const auto &param = param_defs[i];
        std::string param_name = param.name();

        // Check if this parameter exists in globals
        if (!req.globals().map().contains(param_name)) {
          std::string error_msg =
              fmt::format("Missing required parameter '{}' (declared in "
                          "type_manifest but not provided in globals)",
                          param_name);
          LOG_ERROR("SERVER", "MEASURE",
                    "Missing required parameter '%s' for typed main function",
                    param_name.c_str());
          throw std::runtime_error(error_msg);
        }

        // Convert JSON value to Lua object
        sol::object arg =
            variable_to_lua(lua, &req.globals().map().at(param_name));
        args.push_back(arg);

        LOG_INFO("SERVER", "MEASURE",
                 "Passing parameter '%s' to main function (type: %s)",
                 param_name.c_str(), LuaTypes_Name(param.type()).c_str());
        const auto &value = req.globals().map().find(param_name)->second;
        auto type = param.type();

        if (type == v1::LUA_TYPES_CALL_STACK) {
          // Validate input
          if (value.value_case() != v1::VariableValue::kS) {
            stdrp->set_ok(false);
            err->set_message("CallStack must be a serialized string");
            err->set_code(v1::ERROR_CODE_RUNTIME);
            return 1;
          }

          try {
            arg = callstack_from_serialized(lua, value.s());
          } catch (const std::exception &e) {
            stdrp->set_ok(false);
            err->set_message(std::string("CallStack deserialization failed: ") +
                             e.what());
            err->set_code(v1::ERROR_CODE_RUNTIME);
            return 1;
          }

        } else {
          arg = variable_to_lua(lua, &value);
        }
        args.push_back(arg);

        LOG_INFO("SERVER", "MEASURE",
                 "Passing parameter '%s' to main function (type: %s)",
                 param_name.c_str(), LuaTypes_Name(param.type()).c_str());
      }

      // Check for unused globals (warnings)
      for (const auto &it : req.globals().map()) {
        std::string global_name = it.first;
        bool found = false;

        for (int i = 1; i < param_defs.size(); ++i) {
          if (param_defs[i].name() == global_name) {
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
          lua[global_name] = variable_to_lua(lua, &it.second);
        }
      }

      // Call main with unpacked arguments
      sol::protected_function_result main_result =
          (*main_func)(sol::as_args(args));

      if (!main_result.valid()) {
        sol::error result = main_result;
        std::string error_msg =
            std::string("Script execution error: ") + result.what();
        if (ctx_shared->has_error()) {
          error_msg =
              ctx_shared->get_error() + " (Runtime: " + result.what() + ")";
        }
        stdrp->set_ok(false);
        err->set_message(error_msg);
        err->set_code(v1::ERROR_CODE_RUNTIME);
        return 1;
      }
    }

    // The main function should return results (optional)
    // We still collect results from context:call() operations

    // Get collected results
    const auto &results = ctx_shared->get_results();
    LOG_INFO("SERVER", "MEASURE",
             "Lua script done. Serializing %d results into HTTP response",
             results.size());
    stdrp->set_ok(!ctx_shared->has_error());

    auto *results_out = resp->mutable_results();
    results_out->Reserve((int)results.size());
    for (const auto &r : results) {
      auto *out_chunk = results_out->Add();
      out_chunk->set_instrument_name(
          instrument_call_stack_get_instrument_name(r.target.get()));
      out_chunk->set_verb(instrument_call_stack_get_command(r.target.get()));

      auto *ts = out_chunk->mutable_executed_at();

      auto duration = r.executed_at.time_since_epoch();

      auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
      auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
          duration - seconds);

      ts->set_seconds(seconds.count());
      ts->set_nanos(nanos.count());

      // Return value
      auto *returns = out_chunk->mutable_return_();
      returns->Reserve((int)r.returns.size());
      for (const auto &v : r.returns) {
        auto *tparam = returns->Add();
        VariableValue *var = tparam->mutable_value();
        tparam->set_name(v.name);
        switch (v.type) {
        case PARAM_TYPE_DOUBLE:
          var->set_d(v.value.d_val);
          tparam->set_type(v1::LUA_TYPES_DOUBLE);
          break;
        case PARAM_TYPE_INT64:
          var->set_i(v.value.i64_val);
          tparam->set_type(v1::LUA_TYPES_INT64);
          break;
        case PARAM_TYPE_STRING:
          var->set_s(v.value.str_val);
          tparam->set_type(v1::LUA_TYPES_STRING);
          break;
        case PARAM_TYPE_BOOL:
          var->set_b(v.value.b_val);
          tparam->set_type(v1::LUA_TYPES_BOOL);
          break;
        case PARAM_TYPE_BUFFER: {
          var->set_s(v.value.str_val);
          tparam->set_type(v1::LUA_TYPES_DATA_BUFFER);
          auto meta =
              ipc::DataBufferManager::instance().get_metadata(v.value.str_val);
          if (meta.has_value()) {
            tparam->set_allocated_dbmeta(&meta.value());
          }
          break;
        }
        default:
          break;
        }
      }
    }
    LOG_INFO("SERVER", "MEASURE",
             "Serialization complete. Returning to HTTP handler to send "
             "response");
    if (ctx_shared->has_error()) {
      stdrp->set_ok(false);
      err->set_message(ctx_shared->get_error());
      err->set_code(ERROR_CODE_RUNTIME);
      return 1;
    }
    return 0;
  } catch (const std::exception &e) {
    stdrp->set_ok(false);
    err->set_message(std::string("exception: ") + e.what());
    err->set_code(ERROR_CODE_RUNTIME);
    return 1;
  }
}

int handle_discover(const DiscoverRequest &req, DiscoverResponse *resp) {
  auto *stdrp = resp->mutable_standard_response();
  auto *err = stdrp->mutable_error();

  std::vector<std::string> search_paths;

  if (req.plugin_paths().size() > 0) {
    for (const auto &p : req.plugin_paths()) {
      search_paths.push_back(p);
    }
  } else {
#ifdef _WIN32
    search_paths = {".\\plugins", "."};
#else
    search_paths = {"./plugins", "."};
#endif
  }

  auto &plugin_registry = plugin::PluginRegistry::instance();

  static std::once_flag g_plugins_init_flag;

  std::call_once(g_plugins_init_flag, [search_paths, &plugin_registry]() {
    try {
      plugin_registry.load_builtin_plugins();
      plugin_registry.discover_plugins(search_paths);
    } catch (const std::exception &e) {
      LOG_ERROR("PLUGIN_REGISTRY", "DISCOVER_INIT", "Initialization failed: %s",
                e.what());
    }
  });

  auto protocols = plugin_registry.list_protocols();
  stdrp->set_ok(false);
  for (const auto &name : protocols) {
    resp->add_plugin_names(name);
  }
  for (const auto &p : req.plugin_paths()) {
    resp->add_paths(p);
  }

  return 0;
}

int handle_measure_job(const MeasureJobRequest &req, MeasureJobResponse *resp) {
  auto *stdrp = resp->mutable_standard_response();
  auto *err = stdrp->mutable_error();
  const std::string &script_path = req.script_path();
  if (script_path.empty()) {
    stdrp->set_ok(false);
    err->set_message("missing script_path");
    err->set_code(ERROR_CODE_RUNTIME);
    return 1;
  }
  auto jid = JobManager::instance().submit_measure(req);
  stdrp->set_ok(true);
  resp->set_job_id(jid);
  return 0;
}

int handle_job_status(const JobStatusRequest &req, JobStatusResponse *resp) {
  auto *stdrp = resp->mutable_standard_response();
  auto *err = stdrp->mutable_error();
  uint32_t jid = req.job_id();
  JobInfo info;
  if (!JobManager::instance().get_job_info(jid, info)) {
    stdrp->set_ok(false);
    err->set_message("job not found");
    err->set_code(ERROR_CODE_RUNTIME);
    return 1;
  }
  stdrp->set_ok(true);
  *resp->mutable_job() = info.job;
  return 0;
}

int handle_measure_job_result(const MeasureJobResultRequest &req,
                              MeasureJobResultResponse *resp) {
  auto *stdrp = resp->mutable_standard_response();
  auto *err = stdrp->mutable_error();
  JobID jid = req.job_id();

  JobResults result;
  if (!JobManager::instance().get_job_result(jid, result)) {
    JobInfo info;
    if (!JobManager::instance().get_job_info(jid, info)) {
      stdrp->set_ok(false);
      err->set_message("job not found");
      err->set_code(v1::ERROR_CODE_RUNTIME);
      return 1;
    }

    if (info.job.status() != v1::JOB_STATUS_COMPLETED &&
        info.job.status() != v1::JOB_STATUS_CANCELLED) {
      stdrp->set_ok(false);
      err->set_message("job not completed");
      resp->set_status(info.job.status());
      err->set_code(v1::ERROR_CODE_RUNTIME);
      return 1;
    }

    stdrp->set_ok(false);
    err->set_message("no result available");
    err->set_code(v1::ERROR_CODE_INVALID_ARGUMENT);
    return 1;
  }

  if (!std::holds_alternative<v1::MeasureJobResultResponse>(result)) {
    stdrp->set_ok(false);
    err->set_message("job is not a measure job");
    err->set_code(v1::ERROR_CODE_INVALID_ARGUMENT);
    return 1;
  }

  const auto &measure_resp = std::get<v1::MeasureJobResultResponse>(result);
  *resp = measure_resp;
  stdrp->set_ok(true);
  return 0;
}

int handle_job_list(const JobListRequest & /*req*/, JobListResponse *resp) {
  auto *stdrp = resp->mutable_standard_response();
  auto *err = stdrp->mutable_error();
  auto jobs = JobManager::instance().list_jobs();
  stdrp->set_ok(true);
  for (const auto &j : jobs) {
    resp->mutable_jobs()->emplace(j.first, j.second.job);
  }
  return 0;
}

int handle_cancel_job(const CancelJobRequest &req, CancelJobResponse *resp) {
  auto *stdrp = resp->mutable_standard_response();
  auto *err = stdrp->mutable_error();
  JobID jid = req.job_id();
  bool ok = JobManager::instance().cancel_job(jid);
  stdrp->set_ok(ok);
  if (!ok) {
    err->set_message("failed to cancel job (maybe already finished)");
    err->set_code(v1::ERROR_CODE_RUNTIME);
  }
  return ok ? 0 : 1;
}

int handle_list_buffers(const ListDataBuffersRequest &req,
                        ListDataBuffersResponse *resp) {
  auto *stdrp = resp->mutable_standard_response();
  auto *err = stdrp->mutable_error();
  auto &mgr = ipc::DataBufferManager::instance();
  auto buffers = mgr.list_buffers();
  stdrp->set_ok(true);
  for (const auto &id : buffers) {
    if (auto meta = mgr.get_metadata(id)) {
      resp->mutable_buffers()->emplace(id, meta.value());
    }
  }
  return 0;
}

int handle_release_buffer(const ReleaseBufferRequest &req,
                          ReleaseBufferResponse *resp) {
  auto *stdrp = resp->mutable_standard_response();
  auto *err = stdrp->mutable_error();
  std::string id = req.buffer_id();
  if (id.empty()) {
    stdrp->set_ok(false);
    err->set_message("missing buffer_id)");
    err->set_code(v1::ERROR_CODE_INVALID_ARGUMENT);
    return 1;
  }
  auto &mgr = ipc::DataBufferManager::instance();
  mgr.release_buffer(id);
  stdrp->set_ok(true);
  stdrp->set_message("Buffer released successfully");
  return 0;
}

int handle_get_buffer_metadata(const GetBufferMetadataRequest &req,
                               GetBufferMetadataResponse *resp) {
  auto *stdrp = resp->mutable_standard_response();
  auto *err = stdrp->mutable_error();
  std::string id = req.buffer_id();
  if (id.empty()) {
    stdrp->set_ok(false);
    err->set_message("missing buffer_id)");
    err->set_code(v1::ERROR_CODE_INVALID_ARGUMENT);
    return 1;
  }
  auto &mgr = ipc::DataBufferManager::instance();
  auto meta = mgr.get_metadata(id);
  if (!meta.has_value()) {
    stdrp->set_ok(false);
    err->set_message("buffer not found with id: " + id);
    err->set_code(v1::ERROR_CODE_BUFFER_NOT_FOUND);
    return 1;
  }
  stdrp->set_ok(true);
  *resp->mutable_meta() = meta.value();
  return 0;
}
// FIXME: move to CLI
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

  if (meta.data_type() == INST_DATA_FLOAT64) {
    out["data"] = make_vector<double>(data, n);
    out["data_type"] = INST_DATA_FLOAT64;

  } else if (meta.data_type() == INST_DATA_FLOAT32) {
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

int handle_daemon_stop(const DaemonStop &req, void *unused) {
  ServerDaemon::instance().stop();
  return 0;
}
} // namespace instserver::server

