#pragma once
#include "instrument-script-server/export.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace instserver::ipc {

constexpr size_t IPC_MAX_PAYLOAD = 4096;
constexpr size_t IPC_MAX_MESSAGE_SIZE = 8192;

/// IPC message types
struct INSTRUMENT_SERVER_API IPCMessage {
  enum class Type : uint8_t {
    COMMAND = 1,
    RESPONSE = 2,
    HEARTBEAT = 3,
    SHUTDOWN = 4,
    SYNC_ACK = 5,     // Worker -> Server:  "I finished sync command"
    SYNC_CONTINUE = 6 // Server -> Worker: "All workers ready, proceed"
  };

  Type type{};
  uint64_t id{};         // Message/command ID
  uint64_t sync_token{}; // For synchronization across instruments
  uint32_t payload_size{};
  std::array<char, IPC_MAX_PAYLOAD> payload{};

  IPCMessage() { std::memset(payload.data(), 0, payload.size()); }
};

static_assert(sizeof(IPCMessage) <= IPC_MAX_MESSAGE_SIZE,
              "IPCMessage too large for SHM");

} // namespace instserver::ipc
