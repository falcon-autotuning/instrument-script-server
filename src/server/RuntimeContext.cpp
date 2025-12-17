#include "instrument-server/server/RuntimeContext.hpp"
#include "instrument-server/Logger.hpp"

namespace instserver {

RuntimeContextBase::RuntimeContextBase(InstrumentRegistry &registry)
    : registry_(registry) {}

sol::object RuntimeContextBase::call(const std::string &func_name,
                                     sol::variadic_args args,
                                     sol::this_state s) {
  sol::state_view lua(s);

  LOG_DEBUG("LUA_CONTEXT", "CALL", "Calling function: {}", func_name);

  // Parse func_name:  format is "InstrumentID. CommandVerb" or "InstrumentID:
  // Channel.CommandVerb"
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
    channel = std::stoi(instrument_spec.substr(colon_pos + 1));
  }

  // Build params from variadic args
  std::unordered_map<std::string, ParamValue> params;
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
    } else if (arg.is<sol::table>()) {
      // Convert Lua table to map (named parameters)
      sol::table tbl = arg.as<sol::table>();
      for (auto &[k, v] : tbl) {
        std::string param_name = k.as<std::string>();
        if (v.is<double>()) {
          params[param_name] = v.as<double>();
        } else if (v.is<int>()) {
          params[param_name] = static_cast<int64_t>(v.as<int>());
        } else if (v.is<std::string>()) {
          params[param_name] = v.as<std::string>();
        }
      }
    }
  }

  if (channel) {
    params["channel"] = static_cast<int64_t>(*channel);
  }

  // Send command
  CommandResponse resp = send_command(instrument_id, verb, params);

  if (!resp.success) {
    LOG_ERROR("LUA_CONTEXT", "CALL", "Command failed: {}", resp.error_message);
    return sol::nil;
  }

  // Return result
  if (resp.return_value) {
    if (auto d = std::get_if<double>(&*resp.return_value)) {
      return sol::make_object(lua, *d);
    } else if (auto i = std::get_if<int64_t>(&*resp.return_value)) {
      return sol::make_object(lua, *i);
    } else if (auto s = std::get_if<std::string>(&*resp.return_value)) {
      return sol::make_object(lua, *s);
    } else if (auto arr =
                   std::get_if<std::vector<double>>(&*resp.return_value)) {
      sol::table lua_arr = lua.create_table();
      for (size_t i = 0; i < arr->size(); ++i) {
        lua_arr[i + 1] = (*arr)[i];
      }
      return lua_arr;
    }
  }

  return sol::make_object(lua, true); // success
}

void RuntimeContextBase::parallel(sol::function block) {
  LOG_DEBUG("LUA_CONTEXT", "PARALLEL", "Executing parallel block");

  // For now, execute serially (future: spawn threads or use async)
  // In production, collect commands and dispatch them concurrently
  block();
}

void RuntimeContextBase::log(const std::string &msg) {
  LOG_INFO("LUA_SCRIPT", "USER", "{}", msg);
}

CommandResponse RuntimeContextBase::send_command(
    const std::string &instrument_id, const std::string &verb,
    const std::unordered_map<std::string, ParamValue> &params) {
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

  // Determine if query (simplified heuristic)
  cmd.expects_response = (verb.find("Read") != std::string::npos ||
                          verb.find("Get") != std::string::npos ||
                          verb.find("Measure") != std::string::npos);

  return worker->execute_sync(std::move(cmd), std::chrono::milliseconds(5000));
}

RuntimeContext_DCGetSet::RuntimeContext_DCGetSet(InstrumentRegistry &registry)
    : RuntimeContextBase(registry) {}

RuntimeContext_1DWaveform::RuntimeContext_1DWaveform(
    InstrumentRegistry &registry)
    : RuntimeContextBase(registry) {}

RuntimeContext_2DWaveform::RuntimeContext_2DWaveform(
    InstrumentRegistry &registry)
    : RuntimeContextBase(registry) {}

void bind_runtime_contexts(sol::state &lua) {
  // InstrumentTarget
  lua.new_usertype<InstrumentTarget>(
      "InstrumentTarget", sol::constructors<InstrumentTarget()>(), "id",
      &InstrumentTarget::id, "channel", &InstrumentTarget::channel, "serialize",
      &InstrumentTarget::serialize);

  // Domain
  lua.new_usertype<Domain>("Domain", sol::constructors<Domain()>(), "min",
                           &Domain::min, "max", &Domain::max);

  // RuntimeContext_DCGetSet
  lua.new_usertype<RuntimeContext_DCGetSet>(
      "RuntimeContext_DCGetSet",
      sol::constructors<RuntimeContext_DCGetSet(InstrumentRegistry &)>(),
      "getters", &RuntimeContext_DCGetSet::getters, "setters",
      &RuntimeContext_DCGetSet::setters, "sampleRate",
      &RuntimeContext_DCGetSet::sampleRate, "numPoints",
      &RuntimeContext_DCGetSet::numPoints, "call",
      &RuntimeContext_DCGetSet::call, "parallel",
      &RuntimeContext_DCGetSet::parallel, "log", &RuntimeContext_DCGetSet::log,
      "setVoltages",
      sol::property(
          [](RuntimeContext_DCGetSet &self, sol::this_state s) {
            sol::state_view lua(s);
            sol::table tbl = lua.create_table();
            for (const auto &[k, v] : self.setVoltages)
              tbl[k] = v;
            return tbl;
          },
          [](RuntimeContext_DCGetSet &self, sol::table tbl) {
            self.setVoltages.clear();
            for (auto &kv : tbl) {
              self.setVoltages[kv.first.as<std::string>()] =
                  kv.second.as<double>();
            }
          }));

  // RuntimeContext_1DWaveform
  lua.new_usertype<RuntimeContext_1DWaveform>(
      "RuntimeContext_1DWaveform",
      sol::constructors<RuntimeContext_1DWaveform(InstrumentRegistry &)>(),
      "setters", &RuntimeContext_1DWaveform::setters, "bufferedGetters",
      &RuntimeContext_1DWaveform::bufferedGetters, "bufferedSetters",
      &RuntimeContext_1DWaveform::bufferedSetters, "sampleRate",
      &RuntimeContext_1DWaveform::sampleRate, "numPoints",
      &RuntimeContext_1DWaveform::numPoints, "numSteps",
      &RuntimeContext_1DWaveform::numSteps, "call",
      &RuntimeContext_1DWaveform::call, "parallel",
      &RuntimeContext_1DWaveform::parallel, "log",
      &RuntimeContext_1DWaveform::log, "setVoltageDomains",
      sol::property(
          [](RuntimeContext_1DWaveform &self, sol::this_state s) {
            sol::state_view lua(s);
            sol::table tbl = lua.create_table();
            for (const auto &[k, v] : self.setVoltageDomains) {
              sol::table dom = lua.create_table();
              dom["min"] = v.min;
              dom["max"] = v.max;
              tbl[k] = dom;
            }
            return tbl;
          },
          [](RuntimeContext_1DWaveform &self, sol::table tbl) {
            self.setVoltageDomains.clear();
            for (auto &kv : tbl) {
              auto dom_tbl = kv.second.as<sol::table>();
              Domain dom;
              dom.min = dom_tbl["min"];
              dom.max = dom_tbl["max"];
              self.setVoltageDomains[kv.first.as<std::string>()] = dom;
            }
          }));

  // RuntimeContext_2DWaveform
  lua.new_usertype<RuntimeContext_2DWaveform>(
      "RuntimeContext_2DWaveform",
      sol::constructors<RuntimeContext_2DWaveform(InstrumentRegistry &)>(),
      "setters", &RuntimeContext_2DWaveform::setters, "bufferedGetters",
      &RuntimeContext_2DWaveform::bufferedGetters, "bufferedXSetters",
      &RuntimeContext_2DWaveform::bufferedXSetters, "bufferedYSetters",
      &RuntimeContext_2DWaveform::bufferedYSetters, "sampleRate",
      &RuntimeContext_2DWaveform::sampleRate, "numPoints",
      &RuntimeContext_2DWaveform::numPoints, "numXSteps",
      &RuntimeContext_2DWaveform::numXSteps, "numYSteps",
      &RuntimeContext_2DWaveform::numYSteps, "call",
      &RuntimeContext_2DWaveform::call, "parallel",
      &RuntimeContext_2DWaveform::parallel, "log",
      &RuntimeContext_2DWaveform::log, "setXVoltageDomains",
      sol::property(
          [](RuntimeContext_2DWaveform &self, sol::this_state s) {
            sol::state_view lua(s);
            sol::table tbl = lua.create_table();
            for (const auto &[k, v] : self.setXVoltageDomains) {
              sol::table dom = lua.create_table();
              dom["min"] = v.min;
              dom["max"] = v.max;
              tbl[k] = dom;
            }
            return tbl;
          },
          [](RuntimeContext_2DWaveform &self, sol::table tbl) {
            self.setXVoltageDomains.clear();
            for (auto &kv : tbl) {
              auto dom_tbl = kv.second.as<sol::table>();
              Domain dom;
              dom.min = dom_tbl["min"];
              dom.max = dom_tbl["max"];
              self.setXVoltageDomains[kv.first.as<std::string>()] = dom;
            }
          }),
      "setYVoltageDomains",
      sol::property(
          [](RuntimeContext_2DWaveform &self, sol::this_state s) {
            sol::state_view lua(s);
            sol::table tbl = lua.create_table();
            for (const auto &[k, v] : self.setYVoltageDomains) {
              sol::table dom = lua.create_table();
              dom["min"] = v.min;
              dom["max"] = v.max;
              tbl[k] = dom;
            }
            return tbl;
          },
          [](RuntimeContext_2DWaveform &self, sol::table tbl) {
            self.setYVoltageDomains.clear();
            for (auto &kv : tbl) {
              auto dom_tbl = kv.second.as<sol::table>();
              Domain dom;
              dom.min = dom_tbl["min"];
              dom.max = dom_tbl["max"];
              self.setYVoltageDomains[kv.first.as<std::string>()] = dom;
            }
          }));
}
} // namespace instserver
