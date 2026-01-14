#pragma once
#include "instrument-server/export.h"

#include <cstdint>
#include <cstring>

namespace instserver {
namespace ipc {

constexpr size_t IPC_MAX_PAYLOAD = 4096;

/// IPC message types
struct INSTRUMENT_SERVER_API IPCMessage {
  enum class Type : uint32_t {
    COMMAND = 1,
    RESPONSE = 2,
    HEARTBEAT = 3,
    SHUTDOWN = 4,
    SYNC_ACK = 5,     // Worker -> Server:  "I finished sync command"
    SYNC_CONTINUE = 6 // Server -> Worker: "All workers ready, proceed"
  };

  Type type;
  uint64_t id;         // Message/command ID
  uint64_t sync_token; // For synchronization across instruments
  uint32_t payload_size;
  char payload[IPC_MAX_PAYLOAD];

  IPCMessage() : type(Type::COMMAND), id(0), sync_token(0), payload_size(0) {
    std::memset(payload, 0, sizeof(payload));
  }
};

static_assert(sizeof(IPCMessage) <= 8192, "IPCMessage too large for SHM");

} // namespace ipc
} // namespace instserver
