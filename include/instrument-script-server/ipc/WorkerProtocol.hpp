#pragma once
#include "instrument-script-server/SerializedCommand.hpp"
#include "instrument-script-server/export.h"

#include <nlohmann/json.hpp>
#include <string>

namespace instserver::ipc {
struct INSTRUMENT_SERVER_API TypedParamValue {
  std::string type;
  ParamValue value;
};

/// Helper to convert ParamValue to JSON
INSTRUMENT_SERVER_API nlohmann::json param_value_to_json(const ParamValue &val);

/// Helper to convert JSON to ParamValue
INSTRUMENT_SERVER_API ParamValue json_to_param_value(const nlohmann::json &j);

} // namespace instserver::ipc
