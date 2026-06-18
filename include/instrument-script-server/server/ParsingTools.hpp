#pragma once

#include "instrument-script-server/export.h"
#include <filesystem>
#include <instrument-plugin.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <yaml-cpp/node/node.h>
namespace instserver {
// For unpacking the instrument-api
struct INSTRUMENT_SERVER_API IO {
  uint8_t type{0}; // PARAM_TYPE_<xxx>
  std::string name;
};
struct INSTRUMENT_SERVER_API Command {
  std::string name;
  std::vector<IO> parameters;
  std::vector<IO> returns;
};

struct INSTRUMENT_SERVER_API InstrumentConfig {
  std::string name;
  std::string api_ref;

  std::optional<std::string> address;
  std::optional<int> baudrate;
  std::optional<std::string> custom;
};

IO INSTRUMENT_SERVER_API makeIO(const YAML::Node &node);

IO INSTRUMENT_SERVER_API
parseParam(const YAML::Node &node,
           const std::unordered_map<std::string, IO> &io_lookup);

std::unordered_map<std::string, Command>
    INSTRUMENT_SERVER_API load_api(const std::filesystem::path &api_path);

InstrumentConfig INSTRUMENT_SERVER_API
load_config(const std::filesystem::path &config_path);

constexpr uint8_t mapType(std::string_view type) {
  if (type == "float") {
    return PARAM_TYPE_DOUBLE;
  }
  if (type == "integer") {
    return PARAM_TYPE_INT64;
  }
  if (type == "string") {
    return PARAM_TYPE_STRING;
  }
  if (type == "boolean") {
    return PARAM_TYPE_BOOL;
  }
  if (type == "array") {
    return PARAM_TYPE_BUFFER;
  }

  throw std::runtime_error("Unknown type");
}
} // namespace instserver
