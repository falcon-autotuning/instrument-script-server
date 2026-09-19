#pragma once

#include "instrument-script-server/export.h"
#include <cstdint>
#include <filesystem>
#include <instrument-plugin.h>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>
#include <yaml-cpp/node/node.h>
namespace instserver {
// For unpacking the instrument-api
struct INSTRUMENT_SERVER_API Precision {
  std::optional<double> resolution;
};
enum class Notation : uint8_t { Auto, Fixed, Scientific, Engineering };

struct INSTRUMENT_SERVER_API Format {
  std::optional<int> decimal_places;
  std::optional<int> significant_digits;
  std::optional<std::string> high_representation;
  std::optional<std::string> low_representation;

  Notation notation = Notation::Auto;
  char exponent_char = 'E';
};
enum class Role : uint8_t {
  Input,
  Output,
  InOut,
  TriggerIn,
  TriggerOut,
  ClockIn,
  ClockOut,
  Setting
};

template <typename T, typename... Ts>
constexpr bool is_one_of(const T &value, const Ts &...candidates) {
  return ((value == candidates) || ...);
}
constexpr bool is_signal_role(Role role) {
  return is_one_of(role, Role::Input, Role::Output, Role::InOut);
}
struct INSTRUMENT_SERVER_API IO {
  uint8_t type{0}; // PARAM_TYPE_<xxx> (as defined by instrument-plugin)
  std::string name;
  Precision precision;
  Format form;
  std::optional<std::variant<int64_t, double>> max;
  std::optional<std::variant<int64_t, double>> min;
  std::optional<Role> role;
  std::optional<std::string> unit;
};
struct INSTRUMENT_SERVER_API Command {
  std::string name;
  std::vector<IO> parameters;
  std::vector<IO> returns;
  std::optional<std::string> group_name;
  std::optional<std::string> temp;
};
// Different valid config types
enum ConfigTypes : uint8_t { OTHER = 0, VISA = 1 };
struct INSTRUMENT_SERVER_API APIType {
  ConfigTypes type;
  std::optional<std::string> name;
};

struct INSTRUMENT_SERVER_API IOConfig {
  std::optional<std::string> unit;
  double offset = 0.0;
  double scale = 1.0;
};
struct INSTRUMENT_SERVER_API ChannelGroupData {
  // These are a set of the different groups
  std::unordered_map<std::string, std::vector<IO>> channel_groups;
  // These are the IO that belong to each of the groups
  std::unordered_map<std::string, std::unordered_map<std::string, IO>>
      channel_group_io_lookup;
};
// Loads just the channel groups from an API
ChannelGroupData INSTRUMENT_SERVER_API
load_channel_groups(const std::filesystem::path &api_path);
// Loads the IO from an API
std::unordered_map<std::string, IO>
    INSTRUMENT_SERVER_API load_io(const std::filesystem::path &api_path);
struct INSTRUMENT_SERVER_API InstrumentConfig {
  std::string name;
  std::string api_ref;
  APIType api_type;
  std::map<std::string, IOConfig> io_config;

  std::optional<std::string> address;
  std::optional<uint32_t> baudrate;
  std::optional<uint32_t> startup_delay;
  std::optional<std::string> custom;
  std::optional<std::vector<std::string>> init_commands;
};

std::unordered_map<std::string, Command>
    INSTRUMENT_SERVER_API load_api(const std::filesystem::path &api_path);

InstrumentConfig INSTRUMENT_SERVER_API
load_config(const std::filesystem::path &config_path);

inline uint8_t mapType(std::string_view type) {
  if (type == "float" || type == "double") {
    return PARAM_TYPE_DOUBLE;
  }
  if (type == "integer" || type == "int" || type == "int64" ||
      type == "int32") {
    return PARAM_TYPE_INT64;
  }
  if (type == "string") {
    return PARAM_TYPE_STRING;
  }
  if (type == "boolean" || type == "bool") {
    return PARAM_TYPE_BOOL;
  }
  if (type == "array" || type == "buffer" || type == "bytes") {
    return PARAM_TYPE_BUFFER;
  }

  throw std::runtime_error("Unknown type '" + std::string(type) + "'");
}
} // namespace instserver
