#include "instrument-server/ipc/WorkerProtocol.hpp"

namespace instserver {
namespace ipc {

nlohmann::json param_value_to_json(const ParamValue &val) {
  return std::visit(
      [](auto &&arg) -> nlohmann::json {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          return nullptr;
        } else if constexpr (std::is_same_v<T, std::vector<double>> ||
                             std::is_same_v<T, std::vector<int32_t>>) {
          return nlohmann::json(arg);
        } else {
          return arg;
        }
      },
      val);
}

ParamValue json_to_param_value(const nlohmann::json &j) {
  if (j.is_null()) {
    return std::monostate{};
  } else if (j.is_boolean()) {
    return j.get<bool>();
  } else if (j.is_number_integer()) {
    return j.get<int64_t>();
  } else if (j.is_number_unsigned()) {
    return j.get<uint64_t>();
  } else if (j.is_number_float()) {
    return j.get<double>();
  } else if (j.is_string()) {
    return j.get<std::string>();
  } else if (j.is_array() && !j.empty()) {
    if (j[0].is_number_float()) {
      return j.get<std::vector<double>>();
    } else if (j[0].is_number_integer()) {
      return j.get<std::vector<int32_t>>();
    }
  }
  return std::monostate{};
}

std::string serialize_command(const SerializedCommand &cmd) {
  nlohmann::json j;
  j["id"] = cmd.id;
  j["instrument_name"] = cmd.instrument_name;
  j["verb"] = cmd.verb;
  j["timeout_ms"] = cmd.timeout.count();
  j["priority"] = cmd.priority;
  j["expects_response"] = cmd.expects_response;

  if (cmd.return_type) {
    j["return_type"] = *cmd.return_type;
  }

  if (cmd.channel_group) {
    j["channel_group"] = *cmd.channel_group;
  }

  if (cmd.channel_number) {
    j["channel_number"] = *cmd.channel_number;
  }

  nlohmann::json params_json = nlohmann::json::object();
  for (const auto &[key, val] : cmd.params) {
    params_json[key] = param_value_to_json(val);
  }
  j["params"] = params_json;

  return j.dump();
}

SerializedCommand deserialize_command(const std::string &json_str) {
  nlohmann::json j = nlohmann::json::parse(json_str);

  SerializedCommand cmd;
  cmd.id = j["id"];
  cmd.instrument_name = j["instrument_name"];
  cmd.verb = j["verb"];
  cmd.timeout = std::chrono::milliseconds(j["timeout_ms"]);
  cmd.priority = j.value("priority", 0);
  cmd.expects_response = j.value("expects_response", false);

  if (j.contains("return_type")) {
    cmd.return_type = j["return_type"];
  }

  if (j.contains("channel_group")) {
    cmd.channel_group = j["channel_group"];
  }

  if (j.contains("channel_number")) {
    cmd.channel_number = j["channel_number"];
  }

  if (j.contains("params")) {
    for (auto &[key, val] : j["params"].items()) {
      cmd.params[key] = json_to_param_value(val);
    }
  }

  cmd.created_at = std::chrono::steady_clock::now();

  return cmd;
}

std::string serialize_response(const CommandResponse &resp) {
  nlohmann::json j;
  j["command_id"] = resp.command_id;
  j["instrument_name"] = resp.instrument_name;
  j["success"] = resp.success;
  j["error_code"] = resp.error_code;
  j["error_message"] = resp.error_message;
  j["text_response"] = resp.text_response;

  if (resp.return_value) {
    j["return_value"] = param_value_to_json(*resp.return_value);
  }

  // Don't serialize binary_response for now (can add base64 encoding if needed)

  return j.dump();
}

CommandResponse deserialize_response(const std::string &json_str) {
  nlohmann::json j = nlohmann::json::parse(json_str);

  CommandResponse resp;
  resp.command_id = j["command_id"];
  resp.instrument_name = j["instrument_name"];
  resp.success = j["success"];
  resp.error_code = j["error_code"];
  resp.error_message = j["error_message"];
  resp.text_response = j.value("text_response", "");

  if (j.contains("return_value")) {
    resp.return_value = json_to_param_value(j["return_value"]);
  }

  resp.started = std::chrono::steady_clock::now();
  resp.finished = std::chrono::steady_clock::now();

  return resp;
}

} // namespace ipc
} // namespace instserver
