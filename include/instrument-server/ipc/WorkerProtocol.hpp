#pragma once
#include "../SerializedCommand.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace instserver {
namespace ipc {

/// Serialize SerializedCommand to JSON string
std::string serialize_command(const SerializedCommand &cmd);

/// Deserialize JSON string to SerializedCommand
SerializedCommand deserialize_command(const std::string &json_str);

/// Serialize CommandResponse to JSON string
std::string serialize_response(const CommandResponse &resp);

/// Deserialize JSON string to CommandResponse
CommandResponse deserialize_response(const std::string &json_str);

/// Helper to convert ParamValue to JSON
nlohmann::json param_value_to_json(const ParamValue &val);

/// Helper to convert JSON to ParamValue
ParamValue json_to_param_value(const nlohmann::json &j);

} // namespace ipc
} // namespace instserver
