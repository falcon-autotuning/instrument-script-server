#pragma once
#include "instrument-server/export.h"

#include <chrono>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace instserver {

using ParamValue =
    std::variant<double, int64_t, std::string, bool, std::vector<double>>;

struct INSTRUMENT_SERVER_API SerializedCommand {
  std::string id;
  std::string instrument_name;
  std::string verb;
  std::unordered_map<std::string, ParamValue> params;
  bool expects_response{false};
  std::chrono::milliseconds timeout{5000};
  std::chrono::steady_clock::time_point created_at;

  // Synchronization fields
  std::optional<uint64_t> sync_token; // Groups commands in parallel block
  bool is_sync_barrier{false};        // Marks end of sync group
};

struct INSTRUMENT_SERVER_API CommandResponse {
  std::string command_id;
  std::string instrument_name;
  bool success{false};
  int error_code{0};
  std::string error_message;
  std::string text_response;
  std::optional<ParamValue> return_value;

  // Large data buffer fields
  bool has_large_data{false};
  std::string buffer_id;
  uint64_t element_count{0};
  std::string data_type;
};

} // namespace instserver

// Serialization functions in ipc namespace
namespace instserver {
namespace ipc {

/// Serialize command to JSON string
INSTRUMENT_SERVER_API std::string
serialize_command(const SerializedCommand &cmd);

/// Deserialize command from JSON string
INSTRUMENT_SERVER_API SerializedCommand
deserialize_command(const std::string &json);

/// Serialize response to JSON string
INSTRUMENT_SERVER_API std::string
serialize_response(const CommandResponse &resp);

/// Deserialize response from JSON string
INSTRUMENT_SERVER_API CommandResponse
deserialize_response(const std::string &json);

} // namespace ipc
} // namespace instserver
