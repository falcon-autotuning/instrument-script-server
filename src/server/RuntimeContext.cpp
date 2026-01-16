#include "instrument-server/server/RuntimeContext.hpp"
#include "instrument-server/Logger.hpp"
#include <fmt/format.h>
#include <set>
#include <variant>

using namespace instserver;

// Helper to map stored ParamValue to the external return_type string tests
// expect
static std::string param_value_type_name(const ParamValue &val) {
  if (std::get_if<double>(&val)) {
    return "float";
  } else if (std::get_if<int64_t>(&val)) {
    return "integer";
  } else if (std::get_if<std::string>(&val)) {
    return "string";
  } else if (std::get_if<bool>(&val)) {
    return "boolean";
  } else if (std::get_if<std::vector<double>>(&val)) {
    return "array";
  }
  return "unknown";
}

namespace instserver {

RuntimeContext::RuntimeContext(InstrumentRegistry &registry,
                               SyncCoordinator &sync_coordinator,
                               bool enqueue_mode)
    : registry_(registry), sync_coordinator_(sync_coordinator),
      enqueue_mode_(enqueue_mode) {}

static void populate_callresult_from_response(CallResult &cr,
                                              const CommandResponse &resp) {
  cr.command_id = resp.command_id;
  cr.success = resp.success;
  cr.error_message = resp.error_message;
  if (resp.has_large_data) {
    cr.has_large_data = true;
    cr.buffer_id = resp.buffer_id;
    cr.element_count = resp.element_count;
    cr.data_type = resp.data_type;
    cr.return_type = "buffer";
  } else if (resp.return_value) {
    // Copy value and map variant type to a textual type name the rest of the
    // code/tests expect.
    cr.return_value = resp.return_value;
    if (std::get_if<double>(&*resp.return_value)) {
      cr.return_type = "float";
    } else if (std::get_if<int64_t>(&*resp.return_value)) {
      cr.return_type = "integer";
    } else if (std::get_if<std::string>(&*resp.return_value)) {
      cr.return_type = "string";
    } else if (std::get_if<bool>(&*resp.return_value)) {
      cr.return_type = "boolean";
    } else if (std::get_if<std::vector<double>>(&*resp.return_value)) {
      cr.return_type = "array";
      // Provide small array metadata in the CallResult for consumers/tests
      if (auto arr = std::get_if<std::vector<double>>(&*resp.return_value)) {
        cr.element_count = static_cast<uint64_t>(arr->size());
        cr.data_type = "float";
      }
    } else {
      cr.return_type = "unknown";
    }
  } else {
    // No return value and not a large-data buffer: set a default to avoid
    // empty return_type in collected results (tests check non-empty).
    cr.return_type = "void";
  }
}

sol::object RuntimeContext::call(const std::string &func_name,
                                 sol::variadic_args args, sol::this_state s) {
  sol::state_view lua(s);

  LOG_DEBUG("LUA_CONTEXT", "CALL", "Calling function: {}", func_name);

  size_t dot_pos = func_name.find('.');
  if (dot_pos == std::string::npos) {
    LOG_ERROR("LUA_CONTEXT", "CALL", "Invalid function name format: {}",
              func_name);
    return sol::nil;
  }

  std::string instrument_spec = func_name.substr(0, dot_pos);
  std::string verb = func_name.substr(dot_pos + 1);

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

  std::unordered_map<std::string, ParamValue> params;

  if (args.size() == 1 && args[0].is<sol::table>()) {
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

  bool expects_response =
      registry_.command_expects_response(instrument_id, verb);

  // Buffer when inside parallel block
  if (in_parallel_block_) {
    SerializedCommand cmd;
    cmd.instrument_name = instrument_id;
    cmd.verb = verb;
    cmd.params = params;
    cmd.expects_response = expects_response;
    cmd.created_at = std::chrono::steady_clock::now();

    parallel_buffer_.push_back(std::move(cmd));
    LOG_DEBUG("LUA_CONTEXT", "PARALLEL", "Buffered parallel command: {}.{}",
              instrument_id, verb);
    return sol::nil;
  }

  // Enqueue-mode per-call behavior (single-call tokens)
  if (enqueue_mode_ && !in_parallel_block_) {
    auto worker = registry_.get_instrument(instrument_id);
    if (!worker) {
      LOG_ERROR("LUA_CONTEXT", "CALL", "Instrument not found: {}",
                instrument_id);
      return sol::nil;
    }

    SerializedCommand cmd;
    cmd.instrument_name = instrument_id;
    cmd.verb = verb;
    cmd.params = params;
    cmd.expects_response = expects_response;
    cmd.created_at = std::chrono::steady_clock::now();

    // Single-call token
    uint64_t token = next_sync_token_.fetch_add(1);

    // Register barrier across all instruments (global participation)
    auto all_instruments = registry_.list_instruments();
    sync_coordinator_.register_barrier(token, all_instruments);

    token_order_.push_back(token);
    for (auto &inst : all_instruments)
      token_instruments_[token].insert(inst);

    // tag command with sync token and mark as sync barrier for this instrument
    cmd.sync_token = token;
    cmd.is_sync_barrier = true; // single call -> barrier on this cmd

    // record placeholder result index
    CallResult cr;
    cr.command_id = "";
    cr.instrument_name =
        instrument_spec; // Preserve channel addressing like "MockInstrument1:1"
    cr.verb = verb;
    cr.params = params;
    cr.executed_at = std::chrono::steady_clock::now();
    size_t result_index = collected_results_.size();
    collected_results_.push_back(cr);
    token_result_indices_[token].push_back(result_index);

    auto fut = worker->execute(std::move(cmd));
    token_futures_[token].push_back(std::move(fut));

    return sol::nil;
  }

  // Synchronous path
  CommandResponse resp =
      send_command(instrument_id, verb, params, expects_response);

  CallResult cr;
  populate_callresult_from_response(cr, resp);
  cr.instrument_name =
      instrument_spec; // Preserve channel addressing like "MockInstrument1:1"
  cr.verb = verb;
  cr.params = params;
  cr.executed_at = std::chrono::steady_clock::now();

  collected_results_.push_back(cr);

  if (!resp.success) {
    LOG_ERROR("LUA_CONTEXT", "CALL", "Command failed: {}", resp.error_message);
    return sol::nil;
  }

  if (!resp.return_value && !resp.has_large_data)
    return sol::nil;

  if (resp.has_large_data) {
    // No direct Lua return for large buffers
    return sol::nil;
  }

  // Map return types into Lua
  if (auto d = std::get_if<double>(&*resp.return_value)) {
    return sol::make_object(lua, *d);
  } else if (auto i = std::get_if<int64_t>(&*resp.return_value)) {
    return sol::make_object(lua, *i);
  } else if (auto s = std::get_if<std::string>(&*resp.return_value)) {
    return sol::make_object(lua, *s);
  } else if (auto b = std::get_if<bool>(&*resp.return_value)) {
    return sol::make_object(lua, *b);
  }

  return sol::nil;
}

void RuntimeContext::parallel(sol::function block) {
  if (in_parallel_block_) {
    LOG_ERROR("LUA_CONTEXT", "PARALLEL",
              "Nested parallel blocks are not supported");
    return;
  }

  in_parallel_block_ = true;
  parallel_buffer_.clear();

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

  if (parallel_buffer_.empty()) {
    LOG_INFO("LUA_CONTEXT", "PARALLEL", "Executing 0 buffered commands");
    return;
  }

  if (enqueue_mode_) {
    // Create token
    uint64_t token = next_sync_token_.fetch_add(1);

    // Determine instruments participating in this token:
    // - include all registered instruments (global participation)
    auto all_instruments = registry_.list_instruments();
    sync_coordinator_.register_barrier(token, all_instruments);
    token_order_.push_back(token);
    for (auto &inst : all_instruments)
      token_instruments_[token].insert(inst);

    // Group buffered commands by instrument
    std::unordered_map<std::string, std::vector<SerializedCommand>> per_inst;
    for (auto &cmd : parallel_buffer_) {
      per_inst[cmd.instrument_name].push_back(cmd);
    }

    // For each registered instrument:
    for (const auto &inst : all_instruments) {
      auto it = per_inst.find(inst);
      if (it == per_inst.end()) {
        // No real command for this instrument -> send NOP barrier command
        SerializedCommand nop;
        nop.instrument_name = inst;
        nop.verb = "__BARRIER_NOP__";
        nop.params = {};
        nop.expects_response = true;
        nop.created_at = std::chrono::steady_clock::now();
        nop.sync_token = token;
        nop.is_sync_barrier = true;

        // placeholder result
        CallResult cr;
        cr.command_id = "";
        cr.instrument_name = inst;
        cr.verb = nop.verb;
        cr.executed_at = std::chrono::steady_clock::now();
        size_t result_index = collected_results_.size();
        collected_results_.push_back(cr);
        token_result_indices_[token].push_back(result_index);

        auto worker = registry_.get_instrument(inst);
        if (worker) {
          token_futures_[token].push_back(worker->execute(std::move(nop)));
        }
      } else {
        // Send each command for this instrument in order; mark last as barrier
        auto &vec = it->second;
        for (size_t i = 0; i < vec.size(); ++i) {
          SerializedCommand cmd = vec[i];
          cmd.sync_token = token;
          if (i + 1 == vec.size()) {
            cmd.is_sync_barrier = true;
          } else {
            cmd.is_sync_barrier = false;
          }

          CallResult cr;
          cr.command_id = "";
          // Reconstruct display name with channel if present
          std::string display_name = cmd.instrument_name;
          auto channel_it = cmd.params.find("channel");
          if (channel_it != cmd.params.end()) {
            if (auto ch = std::get_if<int64_t>(&channel_it->second)) {
              display_name += ":" + std::to_string(*ch);
            }
          }
          cr.instrument_name = display_name;
          cr.verb = cmd.verb;
          cr.params = cmd.params;
          cr.executed_at = std::chrono::steady_clock::now();
          size_t result_index = collected_results_.size();
          collected_results_.push_back(cr);
          token_result_indices_[token].push_back(result_index);

          auto worker = registry_.get_instrument(cmd.instrument_name);
          if (worker) {
            token_futures_[token].push_back(worker->execute(std::move(cmd)));
          }
        }
      }
    }

    // Done dispatching token commands across all instruments
    return;
  }

  // Non-enqueue fallback: execute & wait
  execute_parallel_buffer();
}

void RuntimeContext::execute_parallel_buffer() {
  if (parallel_buffer_.empty()) {
    return;
  }

  uint64_t sync_token = next_sync_token_++;

  std::vector<std::string> instruments;
  std::set<std::string> unique_instruments;
  for (const auto &cmd : parallel_buffer_) {
    if (unique_instruments.insert(cmd.instrument_name).second) {
      instruments.push_back(cmd.instrument_name);
    }
  }

  sync_coordinator_.register_barrier(sync_token, instruments);

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

  LOG_DEBUG("LUA_CONTEXT", "PARALLEL", "Waiting for {} futures",
            futures.size());

  // Wait for futures first, populate results
  for (size_t i = 0; i < futures.size(); ++i) {
    try {
      auto resp = futures[i].get();
      CallResult cr;
      populate_callresult_from_response(cr, resp);
      cr.executed_at = std::chrono::steady_clock::now();
      collected_results_.push_back(cr);
      if (!resp.success) {
        LOG_ERROR("LUA_CONTEXT", "PARALLEL", "Parallel command failed: {}",
                  resp.error_message);
      }
    } catch (const std::exception &e) {
      LOG_ERROR("LUA_CONTEXT", "PARALLEL", "Future exception: {}", e.what());
    }
  }

  // After responses are in, send SYNC_CONTINUE to all instruments
  for (const auto &inst_name : instruments) {
    auto worker = registry_.get_instrument(inst_name);
    if (worker) {
      worker->send_sync_continue(sync_token);
      LOG_DEBUG("LUA_CONTEXT", "PARALLEL",
                "Sent SYNC_CONTINUE to {} for token={}", inst_name, sync_token);
    }
  }

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

void RuntimeContext::process_tokens_and_wait() {
  for (auto token : token_order_) {
    auto it_futs = token_futures_.find(token);
    auto it_inds = token_result_indices_.find(token);

    if (it_futs != token_futures_.end()) {
      auto &futs = it_futs->second;
      for (size_t i = 0; i < futs.size(); ++i) {
        try {
          auto resp = futs[i].get();
          size_t result_index = 0;
          if (it_inds != token_result_indices_.end() &&
              i < it_inds->second.size()) {
            result_index = it_inds->second[i];
          } else {
            result_index = collected_results_.size();
            collected_results_.push_back(CallResult());
          }

          auto &cr = collected_results_[result_index];
          populate_callresult_from_response(cr, resp);
          cr.executed_at = std::chrono::steady_clock::now();
        } catch (const std::exception &e) {
          LOG_ERROR("LUA_CONTEXT", "TOKEN",
                    "Exception waiting future for token {}: {}", token,
                    e.what());
        }
      }
    }

    // Now send SYNC_CONTINUE to all instruments in the token
    auto it_inst = token_instruments_.find(token);
    if (it_inst != token_instruments_.end()) {
      for (const auto &inst : it_inst->second) {
        auto worker = registry_.get_instrument(inst);
        if (worker) {
          worker->send_sync_continue(token);
          LOG_DEBUG("LUA_CONTEXT", "TOKEN",
                    "Sent SYNC_CONTINUE for token {} to {}", token, inst);
        }
      }
    }

    try {
      sync_coordinator_.clear_barrier(token);
    } catch (...) {
      LOG_WARN("LUA_CONTEXT", "TOKEN",
               "Exception clearing barrier for token {}", token);
    }
  }

  token_order_.clear();
  token_instruments_.clear();
  token_futures_.clear();
  token_result_indices_.clear();
}

nlohmann::json RuntimeContext::collect_results_json() const {
  nlohmann::json out = nlohmann::json::array();
  for (const auto &cr : collected_results_) {
    nlohmann::json j;
    j["command_id"] = cr.command_id;
    j["instrument"] = cr.instrument_name;
    j["verb"] = cr.verb;
    j["executed_at_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                              cr.executed_at.time_since_epoch())
                              .count();
    if (cr.has_large_data) {
      j["return"] = {{"type", "buffer"},
                     {"buffer_id", cr.buffer_id},
                     {"element_count", cr.element_count},
                     {"data_type", cr.data_type}};
    } else if (cr.return_value) {
      if (auto d = std::get_if<double>(&*cr.return_value)) {
        j["return"] = {{"type", "float"}, {"value", *d}};
      } else if (auto i = std::get_if<int64_t>(&*cr.return_value)) {
        j["return"] = {{"type", "integer"}, {"value", *i}};
      } else if (auto s = std::get_if<std::string>(&*cr.return_value)) {
        j["return"] = {{"type", "string"}, {"value", *s}};
      } else if (auto b = std::get_if<bool>(&*cr.return_value)) {
        j["return"] = {{"type", "boolean"}, {"value", *b}};
      } else {
        j["return"] = {{"type", "void"}};
      }
    } else {
      j["return"] = {{"type", "void"}};
    }
    if (!cr.success) {
      j["error"] = cr.error_message;
    }
    out.push_back(j);
  }
  return out;
}

std::shared_ptr<RuntimeContext>
bind_runtime_context(sol::state &lua, InstrumentRegistry &registry,
                     SyncCoordinator &sync_coordinator, bool enqueue_mode) {
  lua.new_usertype<RuntimeContext>(
      "RuntimeContext", sol::no_constructor, "call", &RuntimeContext::call,
      "parallel", &RuntimeContext::parallel, "log", &RuntimeContext::log);

  auto ctx = std::make_shared<RuntimeContext>(registry, sync_coordinator,
                                              enqueue_mode);
  lua["context"] = ctx;
  return ctx;
}

} // namespace instserver
