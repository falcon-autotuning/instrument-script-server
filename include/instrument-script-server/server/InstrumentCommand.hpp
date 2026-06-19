#pragma once
#include "instrument-script-server/ErrorCodes.hpp"
#include "instrument-script-server/export.h"
#include "instrument-script-server/ipc/IPCMessage.hpp"
#include <chrono>
#include <optional>
#include <string>
#include <vector>

// The timeout for a measurement to become stale
extern INSTRUMENT_SERVER_API int g_measurement_timeout_sec;
namespace instserver {

struct INSTRUMENT_SERVER_API InstrumentCommand {
  // these first two arguments are known on the ISS and not worker processes
  std::string instrument_name;
  bool expects_response{false};

  // known on all
  std::string id;
  std::string verb;
  std::vector<Variable> params;
  std::chrono::milliseconds timeout{g_measurement_timeout_sec * 1000};
  std::chrono::steady_clock::time_point created_at;

  // Synchronization fields
  std::optional<uint64_t> sync_token; // Groups commands in parallel block
};

struct INSTRUMENT_SERVER_API InstrumentCommandResponse {
  std::string id;
  ErrorCode error_code{0};
  std::vector<Variable> returns;
};

} // namespace instserver

// Serialization functions in ipc namespace

namespace instserver::ipc {

// Command conversions
void INSTRUMENT_SERVER_API fill_ipc_commands(std::vector<IPCMessage> &out,
                                             const InstrumentCommand &in);
InstrumentCommand INSTRUMENT_SERVER_API
from_ipc_commands(const std::vector<IPCMessage> &in);

// Response conversions
void INSTRUMENT_SERVER_API fill_ipc_responses(
    std::vector<IPCMessage> &out, const InstrumentCommandResponse &in);
InstrumentCommandResponse INSTRUMENT_SERVER_API
from_ipc_responses(const std::vector<IPCMessage> &in);

} // namespace instserver::ipc
