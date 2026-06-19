#include "instrument-script-server/server/RuntimeContext.hpp"
#include "instrument-script-server/ipc/DataBufferManager.hpp"
#include "instrument-script-server/server/InstrumentCommand.hpp"
#include "instrument-script-server/server/ServerDaemon.hpp"
#include "instrument-script-server/server/SyncCoordinator.hpp"
#include <fmt/format.h>
#include <instrument-call-stack/instrument-call-stack-lua.h>
#include <instrument-data.h>
#include <instrument-log/inst_logging.h>
#include <instrument-plugin.h>
using namespace instserver;

// Helper to map stored ParamValue to the external return_type string tests
// expect
static std::string param_value_type_name(const Variable &val) {
  switch (val.type) {
  case PARAM_TYPE_DOUBLE:
    return "double";

  case PARAM_TYPE_INT64:
    return "integer";

  case PARAM_TYPE_STRING:
    return "string";

  case PARAM_TYPE_BOOL:
    return "boolean";

  case PARAM_TYPE_BUFFER:
    return "buffer";
  }

  return "unknown";
}
static void copy_string(char *dst, size_t dst_size, const std::string &src) {
  std::strncpy(dst, src.c_str(), dst_size - 1);
  dst[dst_size - 1] = '\0';
}

namespace instserver {

// BufferHandle implementation

BufferHandle::BufferHandle(const std::string &buffer_id, uint64_t element_count,
                           const std::string &data_type)
    : buffer_id_(buffer_id), element_count_(element_count),
      data_type_(data_type) {
  ipc::DataBufferManager::instance().save_buffer(buffer_id);
}

BufferHandle::~BufferHandle() {}

bool BufferHandle::add_offset(double offset) {
  return ipc::DataBufferManager::instance().add_offset(buffer_id_, offset);
}

bool BufferHandle::multiply_gain(double gain) {
  return ipc::DataBufferManager::instance().multiply_gain(buffer_id_, gain);
}

// MeasurementResponse implementation

MeasurementResponse::MeasurementResponse(CallStackPtr target,
                                         double value_double)
    : target_(std::move(target)), type_("float"), value_double_(value_double) {}

MeasurementResponse::MeasurementResponse(CallStackPtr target, int64_t value_int)
    : target_(std::move(target)), type_("integer"), value_int_(value_int) {}

MeasurementResponse::MeasurementResponse(CallStackPtr target,
                                         const std::string &value_str)
    : target_(std::move(target)), type_("string"), value_str_(value_str) {}

MeasurementResponse::MeasurementResponse(CallStackPtr target, bool value_bool)
    : target_(std::move(target)), type_("boolean"), value_bool_(value_bool) {}

MeasurementResponse::MeasurementResponse(CallStackPtr target,
                                         std::shared_ptr<BufferHandle> buffer)
    : target_(std::move(target)), type_("buffer"), buffer_(std::move(buffer)) {}

sol::object MeasurementResponse::value(sol::this_state s) const {
  sol::state_view lua(s);

  if (type_ == "float") {
    return sol::make_object(lua, value_double_);
  }
  if (type_ == "integer") {
    return sol::make_object(lua, value_int_);
  }
  if (type_ == "string") {
    return sol::make_object(lua, value_str_);
  }
  if (type_ == "boolean") {
    return sol::make_object(lua, value_bool_);
  }
  if (type_ == "buffer") {
    return sol::make_object(lua, buffer_);
  }

  return sol::nil;
}
namespace {
CallStackPtr clone_callstack_ptr(const CallStackPtr &src) {
  return CallStackPtr{instrument_call_stack_clone(src.get()),
                      instrument_call_stack_free};
}
} // namespace

std::shared_ptr<MeasurementResponse>
MeasurementResponse::add_offset(double offset) const {

  if (type_ == "float") {
    return std::make_shared<MeasurementResponse>(clone_callstack_ptr(target_),
                                                 value_double_ + offset);
  }

  if (type_ == "integer") {
    return std::make_shared<MeasurementResponse>(
        clone_callstack_ptr(target_),
        static_cast<int64_t>(value_int_ + offset));
  }

  if (type_ == "buffer" && buffer_) {
    buffer_->add_offset(offset);
    return std::make_shared<MeasurementResponse>(clone_callstack_ptr(target_),
                                                 buffer_);
  }

  LOG_WARN("LUA_CONTEXT", "MATH", "add_offset called on non-numeric type: %s",
           type_.c_str());

  if (type_ == "string") {
    return std::make_shared<MeasurementResponse>(clone_callstack_ptr(target_),
                                                 value_str_);
  }

  if (type_ == "boolean") {
    return std::make_shared<MeasurementResponse>(clone_callstack_ptr(target_),
                                                 value_bool_);
  }

  return std::make_shared<MeasurementResponse>(clone_callstack_ptr(target_),
                                               0.0);
}

std::shared_ptr<MeasurementResponse>
MeasurementResponse::multiply_gain(double gain) const {

  if (type_ == "float") {
    return std::make_shared<MeasurementResponse>(clone_callstack_ptr(target_),
                                                 value_double_ * gain);
  }

  if (type_ == "integer") {
    return std::make_shared<MeasurementResponse>(
        clone_callstack_ptr(target_), static_cast<int64_t>(value_int_ * gain));
  }

  if (type_ == "buffer" && buffer_) {
    // Apply gain in-place
    buffer_->multiply_gain(gain);

    return std::make_shared<MeasurementResponse>(clone_callstack_ptr(target_),
                                                 buffer_);
  }

  LOG_WARN("LUA_CONTEXT", "MATH",
           "multiply_gain called on non-numeric type: %s", type_.c_str());

  if (type_ == "string") {
    return std::make_shared<MeasurementResponse>(clone_callstack_ptr(target_),
                                                 value_str_);
  }

  if (type_ == "boolean") {
    return std::make_shared<MeasurementResponse>(clone_callstack_ptr(target_),
                                                 value_bool_);
  }

  return std::make_shared<MeasurementResponse>(clone_callstack_ptr(target_),
                                               0.0);
}

// RuntimeContext implementation

RuntimeContext::RuntimeContext(InstrumentRegistry &registry,
                               SyncCoordinator &sync_coordinator)
    : registry_(registry), sync_coordinator_(sync_coordinator) {}

static void
populate_callresult_from_response(CallResult &cr,
                                  const InstrumentCommandResponse &resp) {

  cr.command_id = resp.id;
  cr.success = resp.error_code == ErrorCode::NONE;
  cr.returns = resp.returns;
}

sol::object RuntimeContext::call(sol::object target, sol::variadic_args args,
                                 sol::this_state s) {
  sol::state_view lua(s);

  CallStack *cs = nullptr;

  if (target.get_type() == sol::type::userdata) {
    target.push(); // push onto Lua stack

    cs = lua_check_callstack(lua, -1);

    lua_pop(lua, 1);
  }

  if (cs == nullptr) {
    LOG_ERROR("LUA_CONTEXT", "CALL", "Expected CallStack");
    return sol::nil;
  }

  std::string instrument_id = instrument_call_stack_get_instrument_name(cs);
  std::string verb = instrument_call_stack_get_command(cs);
  LOG_INFO("LUA_CONTEXT", "CALL", "Calling instrument: %s and command: %s\n",
           instrument_id.c_str(), verb.c_str());

  int raw_channel = instrument_call_stack_get_channel(cs);
  std::optional<int> channel;
  if (raw_channel != -1) {
    channel = raw_channel;
  }

  std::vector<Variable> params;
  auto worker = registry_.get_instrument(instrument_id);

  if (!worker) {
    LOG_ERROR("LUA_CONTEXT", "CALL", "Instrument not found: %s",
              instrument_id.c_str());
    return sol::nil;
  }
  std::vector<IO> parameters = worker->get_parameters(verb);

  // Table-style parameters
  if (args.size() == 1 && args[0].get_type() == sol::type::table) {
    // first allocate the types
    std::map<std::string, uint8_t> lookup_param_types;

    for (const IO &io : parameters) {
      lookup_param_types.emplace(io.name, io.type);
    }

    // then run through the arguments
    sol::table tbl = args[0];

    std::unordered_map<std::string, Variable> unordered_params;
    for (auto &[k, v] : tbl) {
      Variable p;

      std::string key = k.as<std::string>();
      copy_string(p.name, sizeof(p.name), key);
      auto it = lookup_param_types.find(key);
      uint8_t expected_type = (it != lookup_param_types.end()) ? it->second : 0;

      switch (v.get_type()) {

      case sol::type::number: {
        double d = v.as<double>();

        if (expected_type == PARAM_TYPE_INT64) {
          p.type = PARAM_TYPE_INT64;
          p.value.i64_val = static_cast<int64_t>(d);
        } else {
          p.type = PARAM_TYPE_DOUBLE;
          p.value.d_val = d;
        }
        break;
      }

      case sol::type::string:
        if (expected_type == PARAM_TYPE_STRING) {
          p.type = PARAM_TYPE_STRING;
        } else {
          p.type = PARAM_TYPE_BUFFER;
        }
        copy_string(p.value.str_val, sizeof(p.value.str_val),
                    v.as<std::string>());
        break;

      case sol::type::boolean:
        p.type = PARAM_TYPE_BOOL;
        p.value.b_val = v.as<bool>();
        break;

      default:
        continue; // skip unsupported types
      }

      unordered_params.emplace(key, p);
    }
    for (const auto &[k, v] : lookup_param_types) {
      params.push_back(unordered_params.find(k)->second);
    }

  } else {
    // first allocate expected types by index
    std::vector<uint8_t> expected_types;
    expected_types.reserve(parameters.size());
    std::vector<std::string> expected_names;
    expected_names.reserve(parameters.size());

    for (const IO &io : parameters) {
      expected_types.push_back(io.type);
      expected_names.push_back(io.name);
    }

    // Positional arguments
    size_t arg_count = args.size();

    for (size_t i = 0; i < arg_count; ++i) {
      Variable p;
      copy_string(p.name, sizeof(p.name), expected_names[i]);

      auto arg = args[i];

      // safe lookup: index-based
      uint8_t expected_type =
          (i < expected_types.size()) ? expected_types[i] : 0;

      switch (arg.get_type()) {

      case sol::type::number: {
        double d = arg.as<double>();

        if (expected_type == PARAM_TYPE_INT64) {
          p.type = PARAM_TYPE_INT64;
          p.value.i64_val = static_cast<int64_t>(d);
        } else {
          p.type = PARAM_TYPE_DOUBLE;
          p.value.d_val = d;
        }
        break;
      }

      case sol::type::string:
        if (expected_type == PARAM_TYPE_STRING) {
          p.type = PARAM_TYPE_STRING;
        } else {
          p.type = PARAM_TYPE_BUFFER;
        }
        copy_string(p.value.str_val, sizeof(p.value.str_val),
                    arg.as<std::string>());
        break;

      case sol::type::boolean:
        p.type = PARAM_TYPE_BOOL;
        p.value.b_val = arg.as<bool>();
        break;

      default:
        continue; // skip unsupported
      }

      params.push_back(p);
    }
  }

  // Channel injection
  if (channel) {
    Variable p{};
    copy_string(p.name, sizeof(p.name), "channel");
    p.type = PARAM_TYPE_INT64;
    p.value.i64_val = static_cast<int64_t>(*channel);
    params.push_back(p);
  }

  bool expects_response = worker->command_expects_response(verb);

  // Buffer when inside parallel block
  if (in_parallel_block_) {
    InstrumentCommand cmd{};
    cmd.instrument_name = instrument_id;
    cmd.verb = verb;
    cmd.params = params;
    cmd.expects_response = expects_response;
    cmd.created_at = std::chrono::steady_clock::now();

    parallel_buffer_.push_back(std::move(cmd));
    LOG_DEBUG("LUA_CONTEXT", "PARALLEL", "Buffered parallel command: %s.%s",
              instrument_id.c_str(), verb.c_str());
    return sol::nil;
  }

  // Synchronous (blocking) path - execute and return result to Lua
  InstrumentCommandResponse resp =
      send_command(instrument_id, verb, params, expects_response);

  CallResult cr;
  populate_callresult_from_response(cr, resp);
  using CallStackPtr =
      std::unique_ptr<CallStack, decltype(&instrument_call_stack_free)>;
  cr.target =
      CallStackPtr{instrument_call_stack_clone(cs), instrument_call_stack_free};
  cr.params = params;
  cr.executed_at = std::chrono::steady_clock::now();

  collected_results_.push_back(std::move(cr));

  if (resp.error_code != ErrorCode::NONE) {
    LOG_ERROR("LUA_CONTEXT", "CALL", "Command failed with error code: %d",
              resp.error_code);
    return sol::nil;
  }

  if (resp.returns.empty()) {
    return sol::nil;
  }

  sol::table results =
      lua.create_table(static_cast<int>(resp.returns.size()), 0);

  int idx = 1;

  for (const auto &v : resp.returns) {
    std::shared_ptr<MeasurementResponse> response;

    switch (v.type) {

    case PARAM_TYPE_DOUBLE:
      response = std::make_shared<MeasurementResponse>(
          clone_callstack_ptr(cr.target), v.value.d_val);
      break;

    case PARAM_TYPE_INT64:
      response = std::make_shared<MeasurementResponse>(
          clone_callstack_ptr(cr.target), v.value.i64_val);
      break;

    case PARAM_TYPE_STRING:
      response = std::make_shared<MeasurementResponse>(
          clone_callstack_ptr(cr.target), std::string(v.value.str_val));
      break;

    case PARAM_TYPE_BOOL:
      response = std::make_shared<MeasurementResponse>(
          clone_callstack_ptr(cr.target), v.value.b_val);
      break;

    case PARAM_TYPE_BUFFER: {
      auto &mgr = ipc::DataBufferManager::instance();
      mgr.save_buffer(v.value.str_val);
      auto out = mgr.get_metadata(v.value.str_val);
      if (!out.has_value()) {
        LOG_ERROR("LUA_CONTEXT", "CALL", "Invalid buffer id was sent: %s",
                  v.value.str_val);
        response = std::make_shared<MeasurementResponse>(
            clone_callstack_ptr(cr.target), "Bad array");
        break;
      }
      std::string data_type;
      switch (out->data_type) {
      case INST_DATA_FLOAT32:
        data_type = "float32";
        break;
      case INST_DATA_FLOAT64:
        data_type = "float64";
        break;
      case INST_DATA_INT32:
        data_type = "int32";
        break;
      case INST_DATA_INT64:
        data_type = "int64";
        break;
      case INST_DATA_UINT32:
        data_type = "uint32";
        break;
      case INST_DATA_UINT64:
        data_type = "uint64";
        break;
      case INST_DATA_UINT8:
        data_type = "uint8";
        break;
      default:
        data_type = "unknown";
        break;
      }
      auto handle = std::make_shared<BufferHandle>(
          v.value.str_val, out->element_count, data_type);

      response = std::make_shared<MeasurementResponse>(
          clone_callstack_ptr(cr.target), handle);

      try {
        worker->send_buffer_ack(v.value.str_val);
      } catch (const std::exception &e) {
        LOG_ERROR("LUA_CONTEXT", "HANDOFF", "Failed to send BUFFER_ACK: %s",
                  e.what());
      }

      break;
    }

    default:
      continue; // skip unknown types
    }

    if (response) {
      results[idx++] = response;
    }
  }

  return results;
}

void RuntimeContext::parallel(sol::function block) {
  if (in_parallel_block_) {
    // Nested parallel blocks: ignore inner parallel, treat as sequential
    LOG_WARN("LUA_CONTEXT", "PARALLEL",
             "Nested parallel block detected - executing sequentially");
    block(); // Execute the block directly without buffering
    return;
  }

  in_parallel_block_ = true;
  parallel_buffer_.clear();

  try {
    block();
  } catch (const std::exception &e) {
    LOG_ERROR("LUA_CONTEXT", "PARALLEL", "Error in parallel block: %s",
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

  std::vector<std::future<InstrumentCommandResponse>> futures;
  for (auto &cmd : parallel_buffer_) {
    cmd.sync_token = sync_token;

    auto worker = registry_.get_instrument(cmd.instrument_name);
    if (!worker) {
      LOG_ERROR("LUA_CONTEXT", "PARALLEL", "Instrument not found: %s",
                cmd.instrument_name.c_str());
      continue;
    }

    cmd.id = fmt::format(
        "{}-{}", cmd.instrument_name,
        std::chrono::steady_clock::now().time_since_epoch().count());

    LOG_DEBUG("LUA_CONTEXT", "PARALLEL",
              "Dispatching sync command:  %s to %s (token=%llu, "
              "expects_response=%s)",
              cmd.verb.c_str(), cmd.instrument_name.c_str(),
              (unsigned long long)sync_token,
              cmd.expects_response ? "true" : "false");

    futures.push_back(worker->execute(std::move(cmd)));
  }

  LOG_DEBUG("LUA_CONTEXT", "PARALLEL", "Waiting for %d futures",
            futures.size());

  // Wait for futures first, populate results
  for (auto &future : futures) {
    try {
      auto resp = future.get();
      CallResult cr;
      populate_callresult_from_response(cr, resp);
      cr.executed_at = std::chrono::steady_clock::now();
      collected_results_.push_back(std::move(cr));
      if (resp.error_code != ErrorCode::NONE) {
        LOG_ERROR("LUA_CONTEXT", "PARALLEL",
                  "Parallel command failed with error_code: %d",
                  resp.error_code);
      }
    } catch (const std::exception &e) {
      LOG_ERROR("LUA_CONTEXT", "PARALLEL", "Future exception: %s", e.what());
    }
  }

  // After responses are in, send SYNC_CONTINUE to all instruments
  for (const auto &inst_name : instruments) {
    auto worker = registry_.get_instrument(inst_name);
    if (worker) {
      worker->send_sync_continue(sync_token);
      LOG_DEBUG("LUA_CONTEXT", "PARALLEL",
                "Sent SYNC_CONTINUE to %s for token=%llu", inst_name.c_str(),
                (unsigned long long)sync_token);
    }
  }

  sync_coordinator_.clear_barrier(sync_token);

  LOG_INFO("LUA_CONTEXT", "PARALLEL", "Parallel block complete (token=%llu)",
           (unsigned long long)sync_token);
}

void RuntimeContext::log(const std::string &msg) {
  LOG_INFO("LUA_SCRIPT", "USER", "%s", msg.c_str());
}

void RuntimeContext::error(const std::string &msg) {
  has_error_ = true;
  error_message_ = msg;
  LOG_ERROR("LUA_SCRIPT", "USER_ERROR", "%s", msg.c_str());
}
InstrumentCommandResponse RuntimeContext::send_command(
    const std::string &instrument_id, const std::string &verb,
    const std::vector<Variable> &params, bool expects_response) {
  auto worker = registry_.get_instrument(instrument_id);
  if (!worker) {
    InstrumentCommandResponse resp;
    resp.error_code = ErrorCode::INSTRUMENT_NOT_FOUND;
    return resp;
  }

  InstrumentCommand cmd;
  cmd.id =
      fmt::format("{}-{}", instrument_id,
                  std::chrono::steady_clock::now().time_since_epoch().count());
  cmd.instrument_name = instrument_id;
  cmd.verb = verb;

  cmd.params = params;
  cmd.created_at = std::chrono::steady_clock::now();
  cmd.expects_response = expects_response;

  LOG_INFO("LUA_CONTEXT", "SEND", "Sending command %s.%s (expects_response=%s)",
           instrument_id.c_str(), verb.c_str(),
           expects_response ? "true" : "false");

  auto resp = worker->execute_sync(
      std::move(cmd),
      std::chrono::milliseconds(g_measurement_timeout_sec * 1000));
  LOG_INFO("LUA_CONTEXT", "SEND", "Command %s.%s returned: success=%s",
           instrument_id.c_str(), verb.c_str(),
           (resp.error_code == ErrorCode::NONE) ? "true" : "false");
  return resp;
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
            collected_results_.emplace_back();
          }

          auto &cr = collected_results_[result_index];
          populate_callresult_from_response(cr, resp);
          cr.executed_at = std::chrono::steady_clock::now();
        } catch (const std::exception &e) {
          LOG_ERROR("LUA_CONTEXT", "TOKEN",
                    "Exception waiting future for token %llu: %s",
                    (unsigned long long)token, e.what());
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
                    "Sent SYNC_CONTINUE for token %llu to %s",
                    (unsigned long long)token, inst.c_str());
        }
      }
    }

    try {
      sync_coordinator_.clear_barrier(token);
    } catch (...) {
      LOG_WARN("LUA_CONTEXT", "TOKEN",
               "Exception clearing barrier for token %llu",
               (unsigned long long)token);
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
    j["instrument"] =
        instrument_call_stack_get_instrument_name(cr.target.get());
    j["verb"] = instrument_call_stack_get_command(cr.target.get());
    j["executed_at_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                              cr.executed_at.time_since_epoch())
                              .count();
    // Return value
    nlohmann::json return_json;
    for (const auto &v : cr.returns) {
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

        auto out =
            ipc::DataBufferManager::instance().get_metadata(v.value.str_val);

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
    j["return"] = return_json;
    if (!cr.success) {
      j["error"] = cr.error_message;
    }
    out.push_back(j);
  }

  return out;
}

std::shared_ptr<RuntimeContext>
bind_runtime_context(sol::state &lua, InstrumentRegistry &registry,
                     SyncCoordinator &sync_coordinator) {
  // Bind BufferHandle for array operations
  lua.new_usertype<BufferHandle>(
      "BufferHandle", sol::no_constructor, "id", &BufferHandle::id, "size",
      &BufferHandle::size, "type", &BufferHandle::type, "add_offset",
      &BufferHandle::add_offset, "multiply_gain", &BufferHandle::multiply_gain);

  // Bind MeasurementResponse - wraps return values with metadata
  lua.new_usertype<MeasurementResponse>(
      "MeasurementResponse", sol::no_constructor, "instrument",
      &MeasurementResponse::instrument, "verb", &MeasurementResponse::verb,
      "type", &MeasurementResponse::type, "value", &MeasurementResponse::value,
      "buffer", &MeasurementResponse::buffer, "add_offset",
      &MeasurementResponse::add_offset, "multiply_gain",
      &MeasurementResponse::multiply_gain);

  // Bind RuntimeContext
  lua.new_usertype<RuntimeContext>(
      "RuntimeContext", sol::no_constructor, "call", &RuntimeContext::call,
      "parallel", &RuntimeContext::parallel, "log", &RuntimeContext::log,
      "error", &RuntimeContext::error);

  auto ctx = std::make_shared<RuntimeContext>(registry, sync_coordinator);
  lua["context"] = ctx;
  return ctx;
}

} // namespace instserver
