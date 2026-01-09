#include "instrument-server/server/RuntimeContext.hpp"
#include "instrument-server/Logger.hpp"
#include <future>

namespace instserver {

RuntimeContext::RuntimeContext(InstrumentRegistry &registry,
                               SyncCoordinator &sync_coordinator)
    : registry_(registry), sync_coordinator_(sync_coordinator) {}

sol::object RuntimeContext::call(const std::string &func_name,
                                 sol::variadic_args args, sol::this_state s) {
  sol::state_view lua(s);

  LOG_DEBUG("LUA_CONTEXT", "CALL", "Calling function: {}", func_name);

  // Parse func_name:  format is "InstrumentID.CommandVerb" or
  // "InstrumentID:Channel.CommandVerb"
  size_t dot_pos = func_name.find('.');
  if (dot_pos == std::string::npos) {
    LOG_ERROR("LUA_CONTEXT", "CALL", "Invalid function name format: {}",
              func_name);
    return sol::nil;
  }

  std::string instrument_spec = func_name.substr(0, dot_pos);
  std::string verb = func_name.substr(dot_pos + 1);

  // Parse instrument ID and optional channel
  std::string instrument_id = instrument_spec;
  std::optional<int> channel;
  size_t colon_pos = instrument_spec.find(':');
  if (colon_pos != std::string::npos) {
    instrument_id = instrument_spec.substr(0, colon_pos);
    try {
      channel = std::stoi(instrument_spec.substr(colon_pos + 1));
    } catch (const std::exception &e) {
      LOG_ERROR("LUA_CONTEXT", "CALL", "Invalid channel number in:  {}",
                func_name);
      return sol::nil;
    }
  }

  // Build params from variadic args
  std::unordered_map<std::string, ParamValue> params;

  // Handle both positional args and named parameter table
  if (args.size() == 1 && args[0].is<sol::table>()) {
    // Named parameters as table
    sol::table tbl = args[0].as<sol::table>();
    for (auto &[k, v] : tbl) {
      std::string param_name = k.as<std::string>();
      if (v.is<double>()) {
        params[param_name] = v.as<double>();
      } else if (v.is<int>()) {
        params[param_name] = static_cast<int64_t>(v.as<int>());
      } else if (v.is<std::string>()) {
        params[param_name] = v.as<std::string>();
      } else if (v.is<bool>()) {
        params[param_name] = v.as<bool>();
      }
    }
  } else {
    // Positional arguments
    for (size_t i = 0; i < args.size(); ++i) {
      auto arg = args[i];
      std::string key = "arg" + std::to_string(i);

      if (arg.is<double>()) {
        params[key] = arg.as<double>();
      } else if (arg.is<int>()) {
        params[key] = static_cast<int64_t>(arg.as<int>());
      } else if (arg.is<std::string>()) {
        params[key] = arg.as<std::string>();
      } else if (arg.is<bool>()) {
        params[key] = arg.as<bool>();
      }
    }
  }

  if (channel) {
    params["channel"] = static_cast<int64_t>(*channel);
  }

  // Check if command expects response using API definition
  bool expects_response =
      registry_.command_expects_response(instrument_id, verb);

  // If in parallel block, buffer the command instead of executing
  if (in_parallel_block_) {
    SerializedCommand cmd;
    cmd.id =
        fmt::format("{}-buffered-{}", instrument_id, parallel_buffer_.size());
    cmd.instrument_name = instrument_id;
    cmd.verb = verb;
    cmd.params = params;
    cmd.created_at = std::chrono::steady_clock::now();
    cmd.expects_response = expects_response;

    parallel_buffer_.push_back(std::move(cmd));
    LOG_DEBUG("LUA_CONTEXT", "CALL",
              "Buffered parallel command: {}. {} (expects_response={})",
              instrument_id, verb, expects_response);
    return sol::nil; // Don't execute yet
  }

  // Execute immediately for non-parallel calls
  CommandResponse resp =
      send_command(instrument_id, verb, params, expects_response);

  if (!resp.success) {
    LOG_ERROR("LUA_CONTEXT", "CALL", "Command failed: {}", resp.error_message);
    return sol::nil;
  }

  // Collect the result
  CallResult result;
  result.command_id = resp.command_id;
  result.instrument_name = instrument_spec; // Use full spec with channel if present
  result.verb = verb;
  result.params = params;
  result.executed_at = std::chrono::steady_clock::now();
  
  // Handle large data buffer
  if (resp.has_large_data) {
    result.has_large_data = true;
    result.buffer_id = resp.buffer_id;
    result.element_count = resp.element_count;
    result.data_type = resp.data_type;
    result.return_type = "buffer";
  } else if (resp.return_value) {
    // Handle direct return value
    result.return_value = resp.return_value;
    
    // Determine type string
    if (std::holds_alternative<double>(*resp.return_value)) {
      result.return_type = "double";
    } else if (std::holds_alternative<int64_t>(*resp.return_value)) {
      result.return_type = "int64";
    } else if (std::holds_alternative<std::string>(*resp.return_value)) {
      result.return_type = "string";
    } else if (std::holds_alternative<bool>(*resp.return_value)) {
      result.return_type = "bool";
    } else if (std::holds_alternative<std::vector<double>>(*resp.return_value)) {
      result.return_type = "array";
    }
  } else if (expects_response) {
    // Command expected response but didn't return a value - indicates success
    result.return_value = true;
    result.return_type = "bool";
  } else {
    // Command doesn't expect response - mark as void
    result.return_type = "void";
  }
  
  collected_results_.push_back(std::move(result));

  // Return result
  if (resp.return_value) {
    if (auto d = std::get_if<double>(&*resp.return_value)) {
      return sol::make_object(lua, *d);
    } else if (auto i = std::get_if<int64_t>(&*resp.return_value)) {
      return sol::make_object(lua, *i);
    } else if (auto s = std::get_if<std::string>(&*resp.return_value)) {
      return sol::make_object(lua, *s);
    } else if (auto b = std::get_if<bool>(&*resp.return_value)) {
      return sol::make_object(lua, *b);
    } else if (auto arr =
                   std::get_if<std::vector<double>>(&*resp.return_value)) {
      sol::table lua_arr = lua.create_table();
      for (size_t i = 0; i < arr->size(); ++i) {
        lua_arr[i + 1] = (*arr)[i];
      }
      return lua_arr;
    }
  }

  // If expects response but no return value, return success indicator
  if (expects_response) {
    return sol::make_object(lua, true);
  }

  return sol::nil; // Commands that don't expect response return nil
}

void RuntimeContext::parallel(sol::function block) {
  LOG_DEBUG("LUA_CONTEXT", "PARALLEL", "Starting parallel block");

  in_parallel_block_ = true;
  parallel_buffer_.clear();

  // Execute the Lua block, which will buffer commands via call()
  try {
    block();
  } catch (const std::exception &e) {
    LOG_ERROR("LUA_CONTEXT", "PARALLEL", "Error in parallel block:  {}",
              e.what());
    in_parallel_block_ = false;
    parallel_buffer_.clear();
    throw;
  }

  in_parallel_block_ = false;

  LOG_INFO("LUA_CONTEXT", "PARALLEL", "Executing {} buffered commands",
           parallel_buffer_.size());

  // Execute all buffered commands with synchronization
  execute_parallel_buffer();

  parallel_buffer_.clear();
}

void RuntimeContext::execute_parallel_buffer() {
  if (parallel_buffer_.empty()) {
    return;
  }

  // Assign sync token to all commands
  uint64_t sync_token = next_sync_token_++;

  // Collect unique instruments
  std::vector<std::string> instruments;
  std::set<std::string> unique_instruments;
  for (const auto &cmd : parallel_buffer_) {
    if (unique_instruments.insert(cmd.instrument_name).second) {
      instruments.push_back(cmd.instrument_name);
    }
  }

  // Register barrier with sync coordinator
  sync_coordinator_.register_barrier(sync_token, instruments);

  // Tag all commands with sync token and dispatch
  std::vector<std::future<CommandResponse>> futures;
  for (auto &cmd : parallel_buffer_) {
    cmd.sync_token = sync_token;

    auto worker = registry_.get_instrument(cmd.instrument_name);
    if (!worker) {
      LOG_ERROR("LUA_CONTEXT", "PARALLEL", "Instrument not found: {}",
                cmd.instrument_name);
      continue;
    }

    cmd.id = fmt::format(
        "{}-{}", cmd.instrument_name,
        std::chrono::steady_clock::now().time_since_epoch().count());

    LOG_DEBUG(
        "LUA_CONTEXT", "PARALLEL",
        "Dispatching sync command:  {} to {} (token={}, expects_response={})",
        cmd.verb, cmd.instrument_name, sync_token, cmd.expects_response);

    futures.push_back(worker->execute(std::move(cmd)));
  }

  // Wait for all commands to complete
  LOG_DEBUG("LUA_CONTEXT", "PARALLEL", "Waiting for {} futures",
            futures.size());

  for (auto &future : futures) {
    try {
      auto resp = future.get();
      if (!resp.success) {
        LOG_ERROR("LUA_CONTEXT", "PARALLEL", "Parallel command failed: {}",
                  resp.error_message);
      }
    } catch (const std::exception &e) {
      LOG_ERROR("LUA_CONTEXT", "PARALLEL", "Future exception: {}", e.what());
    }
  }

  // No need to query SyncCoordinator again
  for (const auto &inst_name : instruments) {
    auto worker = registry_.get_instrument(inst_name);
    if (worker) {
      worker->send_sync_continue(sync_token);
      LOG_DEBUG("LUA_CONTEXT", "PARALLEL",
                "Sent SYNC_CONTINUE to {} for token={}", inst_name, sync_token);
    }
  }

  // Now clear the barrier
  sync_coordinator_.clear_barrier(sync_token);

  LOG_INFO("LUA_CONTEXT", "PARALLEL", "Parallel block complete (token={})",
           sync_token);
}

void RuntimeContext::log(const std::string &msg) {
  LOG_INFO("LUA_SCRIPT", "USER", "{}", msg);
}

CommandResponse RuntimeContext::send_command(
    const std::string &instrument_id, const std::string &verb,
    const std::unordered_map<std::string, ParamValue> &params,
    bool expects_response) {
  auto worker = registry_.get_instrument(instrument_id);
  if (!worker) {
    CommandResponse resp;
    resp.success = false;
    resp.error_message = "Instrument not found: " + instrument_id;
    return resp;
  }

  SerializedCommand cmd;
  cmd.id =
      fmt::format("{}-{}", instrument_id,
                  std::chrono::steady_clock::now().time_since_epoch().count());
  cmd.instrument_name = instrument_id;
  cmd.verb = verb;
  cmd.params = params;
  cmd.created_at = std::chrono::steady_clock::now();
  cmd.expects_response = expects_response;

  LOG_DEBUG("LUA_CONTEXT", "SEND",
            "Sending command {}.{} (expects_response={})", instrument_id, verb,
            expects_response);

  return worker->execute_sync(std::move(cmd), std::chrono::milliseconds(5000));
}

void bind_runtime_context(sol::state &lua, InstrumentRegistry &registry,
                          SyncCoordinator &sync_coordinator) {
  // Register methods but prevent Lua from constructing directly
  lua.new_usertype<RuntimeContext>(
      "RuntimeContext", sol::no_constructor, "call", &RuntimeContext::call,
      "parallel", &RuntimeContext::parallel, "log", &RuntimeContext::log);

  // Create and inject a ready-to-use context object into Lua as `context`
  // so scripts can do context:call(...) and context:log(...)
  auto ctx = std::make_shared<RuntimeContext>(registry, sync_coordinator);
  lua["context"] = ctx;
}

} // namespace instserver
