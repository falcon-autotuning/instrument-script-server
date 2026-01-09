#include "instrument-server/SerializedCommand.hpp"
#include <nlohmann/json.hpp>

namespace instserver {
namespace ipc {

std::string serialize_command(const SerializedCommand &cmd) {
  nlohmann::json j;
  j["id"] = cmd.id;
  j["instrument_name"] = cmd.instrument_name;
  j["verb"] = cmd.verb;
  j["expects_response"] = cmd.expects_response;
  j["timeout_ms"] = cmd.timeout.count();

  if (cmd.sync_token) {
    j["sync_token"] = *cmd.sync_token;
  }

  // Serialize params
  nlohmann::json params_json = nlohmann::json::object();
  for (const auto &[key, value] : cmd.params) {
    if (auto d = std::get_if<double>(&value)) {
      params_json[key] = *d;
    } else if (auto i = std::get_if<int64_t>(&value)) {
      params_json[key] = *i;
    } else if (auto s = std::get_if<std::string>(&value)) {
      params_json[key] = *s;
    } else if (auto b = std::get_if<bool>(&value)) {
      params_json[key] = *b;
    } else if (auto arr = std::get_if<std::vector<double>>(&value)) {
      params_json[key] = *arr;
    }
  }
  j["params"] = params_json;

  return j.dump();
}

SerializedCommand deserialize_command(const std::string &json) {
  auto j = nlohmann::json::parse(json);

  SerializedCommand cmd;
  cmd.id = j["id"];
  cmd.instrument_name = j["instrument_name"];
  cmd.verb = j["verb"];
  cmd.expects_response = j["expects_response"];
  cmd.timeout = std::chrono::milliseconds(j["timeout_ms"]);
  cmd.created_at = std::chrono::steady_clock::now();

  if (j.contains("sync_token")) {
    cmd.sync_token = j["sync_token"];
  }

  // Deserialize params
  if (j.contains("params") && j["params"].is_object()) {
    for (auto &[key, value] : j["params"].items()) {
      if (value.is_number_float()) {
        cmd.params[key] = value.get<double>();
      } else if (value.is_number_integer()) {
        cmd.params[key] = value.get<int64_t>();
      } else if (value.is_string()) {
        cmd.params[key] = value.get<std::string>();
      } else if (value.is_boolean()) {
        cmd.params[key] = value.get<bool>();
      } else if (value.is_array()) {
        cmd.params[key] = value.get<std::vector<double>>();
      }
    }
  }

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

  // Serialize return value
  if (resp.return_value) {
    if (auto d = std::get_if<double>(&*resp.return_value)) {
      j["return_value"] = *d;
      j["return_type"] = "double";
    } else if (auto i = std::get_if<int64_t>(&*resp.return_value)) {
      j["return_value"] = *i;
      j["return_type"] = "int64";
    } else if (auto s = std::get_if<std::string>(&*resp.return_value)) {
      j["return_value"] = *s;
      j["return_type"] = "string";
    } else if (auto b = std::get_if<bool>(&*resp.return_value)) {
      j["return_value"] = *b;
      j["return_type"] = "bool";
    } else if (auto arr =
                   std::get_if<std::vector<double>>(&*resp.return_value)) {
      j["return_value"] = *arr;
      j["return_type"] = "array";
    }
  }

  // Serialize large data buffer fields
  j["has_large_data"] = resp.has_large_data;
  if (resp.has_large_data) {
    j["buffer_id"] = resp.buffer_id;
    j["element_count"] = resp.element_count;
    j["data_type"] = resp.data_type;
  }

  return j.dump();
}

CommandResponse deserialize_response(const std::string &json) {
  auto j = nlohmann::json::parse(json);

  CommandResponse resp;
  resp.command_id = j["command_id"];
  resp.instrument_name = j["instrument_name"];
  resp.success = j["success"];
  resp.error_code = j["error_code"];
  resp.error_message = j["error_message"];
  resp.text_response = j["text_response"];

  // Deserialize return value
  if (j.contains("return_value") && j.contains("return_type")) {
    std::string type = j["return_type"];
    if (type == "double") {
      resp.return_value = j["return_value"].get<double>();
    } else if (type == "int64") {
      resp.return_value = j["return_value"].get<int64_t>();
    } else if (type == "string") {
      resp.return_value = j["return_value"].get<std::string>();
    } else if (type == "bool") {
      resp.return_value = j["return_value"].get<bool>();
    } else if (type == "array") {
      resp.return_value = j["return_value"].get<std::vector<double>>();
    }
  }

  // Deserialize large data buffer fields
  if (j.contains("has_large_data")) {
    resp.has_large_data = j["has_large_data"];
    if (resp.has_large_data) {
      resp.buffer_id = j["buffer_id"];
      resp.element_count = j["element_count"];
      resp.data_type = j["data_type"];
    }
  }

  return resp;
}

} // namespace ipc
} // namespace instserver
