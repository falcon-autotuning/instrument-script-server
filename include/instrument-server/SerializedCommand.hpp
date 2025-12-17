#pragma once
#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace instserver {

/// Universal parameter type for both VISA strings and native function calls
using ParamValue =
    std::variant<std::monostate, int32_t, int64_t, uint32_t, uint64_t, float,
                 double, bool, std::string, std::vector<uint8_t>,
                 std::vector<double>, std::vector<int32_t>>;

/// SerializedCommand:  universal instruction format
/// Works for both VISA template expansion AND native function call mapping
struct SerializedCommand {
  // Identity
  std::string id;              // Unique instruction ID (UUID)
  std::string instrument_name; // Target instrument
  std::string verb; // Command name (e.g., "SET_VOLTAGE", "DAQmxCreateTask")

  // Parameters (filled from Lua context. call() or C++ API)
  std::unordered_map<std::string, ParamValue> params;

  // Metadata
  std::chrono::milliseconds timeout{5000};
  int priority{0};
  bool expects_response{false};
  std::optional<std::string>
      return_type; // "void", "float", "array<double>", "TaskHandle"

  // Channel info (for multi-channel instruments)
  std::optional<std::string> channel_group;
  std::optional<int> channel_number;

  // Timing
  std::chrono::steady_clock::time_point created_at;

  nlohmann::json to_json() const;
  static SerializedCommand from_json(const nlohmann::json &j);
};

/// Response from command execution
struct CommandResponse {
  std::string command_id;
  std::string instrument_name;
  bool success{false};

  // Return value (if any)
  std::optional<ParamValue> return_value;

  // Raw response data
  std::string text_response;
  std::vector<uint8_t> binary_response;

  // Error info
  int error_code{0};
  std::string error_message;

  // Timing
  std::chrono::steady_clock::time_point started;
  std::chrono::steady_clock::time_point finished;

  std::chrono::microseconds duration() const {
    return std::chrono::duration_cast<std::chrono::microseconds>(finished -
                                                                 started);
  }

  nlohmann::json to_json() const;
};

} // namespace instserver
