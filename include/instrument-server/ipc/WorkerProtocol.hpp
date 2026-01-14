#pragma once
#include "instrument-server/export.h"

#include "../SerializedCommand.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace instserver {
namespace ipc {
struct INSTRUMENT_SERVER_API TypedParamValue {
  std::string type;
  ParamValue value;
};

/// Serialize SerializedCommand to JSON string
INSTRUMENT_SERVER_API std::string
serialize_command(const SerializedCommand &cmd);

/// Deserialize JSON string to SerializedCommand
INSTRUMENT_SERVER_API SerializedCommand
deserialize_command(const std::string &json_str);

/// Serialize CommandResponse to JSON string
INSTRUMENT_SERVER_API std::string
serialize_response(const CommandResponse &resp);

/// Deserialize JSON string to CommandResponse
INSTRUMENT_SERVER_API CommandResponse
deserialize_response(const std::string &json_str);

/// Helper to convert ParamValue to JSON
INSTRUMENT_SERVER_API nlohmann::json param_value_to_json(const ParamValue &val);

/// Helper to convert JSON to ParamValue
INSTRUMENT_SERVER_API ParamValue json_to_param_value(const nlohmann::json &j);

} // namespace ipc
} // namespace instserver
