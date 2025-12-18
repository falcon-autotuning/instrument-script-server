#include "instrument-server/SerializedCommand.hpp"
#include "instrument-server/ipc/WorkerProtocol.hpp"

namespace instserver {

nlohmann::json SerializedCommand::to_json() const {
  nlohmann::json j;

  j["id"] = id;
  j["instrument_name"] = instrument_name;
  j["verb"] = verb;
  j["timeout_ms"] = timeout.count();
  j["priority"] = priority;
  j["expects_response"] = expects_response;

  if (return_type) {
    j["return_type"] = *return_type;
  }

  if (channel_group) {
    j["channel_group"] = *channel_group;
  }

  if (channel_number) {
    j["channel_number"] = *channel_number;
  }

  // Serialize params
  nlohmann::json params_json = nlohmann::json::object();
  for (const auto &[key, val] : params) {
    params_json[key] = ipc::param_value_to_json(val);
  }
  j["params"] = params_json;

  return j;
}

SerializedCommand SerializedCommand::from_json(const nlohmann::json &j) {
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
      cmd.params[key] = ipc::json_to_param_value(val);
    }
  }

  cmd.created_at = std::chrono::steady_clock::now();

  return cmd;
}

nlohmann::json CommandResponse::to_json() const {
  nlohmann::json j;

  j["command_id"] = command_id;
  j["instrument_name"] = instrument_name;
  j["success"] = success;
  j["error_code"] = error_code;
  j["error_message"] = error_message;
  j["text_response"] = text_response;

  if (return_value) {
    j["return_value"] = ipc::param_value_to_json(*return_value);
  }

  // Optionally serialize timing info
  j["duration_us"] = duration().count();

  return j;
}

} // namespace instserver
