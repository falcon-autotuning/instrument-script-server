#pragma once
#include "instrument-script-server/export.h"
#include "instrument-script-server/ipc/IPCMessage.hpp"
#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace instserver {
using ParamType = ipc::IPCParamValue::Type;
struct ParamValue {
  ParamType type;

  double d{};
  int64_t i{};
  bool b{};
  std::string str;
  std::vector<double> arr;
};
struct INSTRUMENT_SERVER_API Param {
  std::string name;
  ParamValue value;
};

struct INSTRUMENT_SERVER_API SerializedCommand {
  std::string id;
  std::string instrument_name;
  std::string verb;
  std::array<Param, PLUGIN_MAX_PARAMS> params;
  uint8_t param_count{0};
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

namespace instserver::ipc {

// Command conversions
void INSTRUMENT_SERVER_API fill_ipc_command(IPCCommand &out,
                                            const SerializedCommand &in);
SerializedCommand INSTRUMENT_SERVER_API from_ipc_command(const IPCCommand &in);

// Response conversions
void INSTRUMENT_SERVER_API fill_ipc_response(IPCResponse &out,
                                             const CommandResponse &in);
CommandResponse INSTRUMENT_SERVER_API from_ipc_response(const IPCResponse &in);

} // namespace instserver::ipc
