#pragma once
#include "instrument-script-server/export.h"
#include <cstdint>
#include <cstring>
#include <instrument-plugin.h>

namespace instserver::ipc {

constexpr size_t IPC_MAX_PAYLOAD = 4096;
constexpr size_t PLUGIN_MAX_ARRAY_LEN = 128;
struct IPCParamValue {
  enum class Type : uint8_t { DOUBLE, INT64, BOOL, STRING, DOUBLE_ARRAY };

  Type type;

  union {
    double d;
    int64_t i;
    bool b;

    char str[PLUGIN_MAX_STRING_LEN];
    // FIX: remove reliance on non data buffer arrays
    struct {
      double data[PLUGIN_MAX_ARRAY_LEN]; // ✅ fixed capacity
      uint32_t size;                     // ✅ actual length
    } arr;
  };
};
// Sending a message over IPC
struct INSTRUMENT_SERVER_API IPCCommand {
  char command_id[PLUGIN_MAX_STRING_LEN];
  char instrument_name[PLUGIN_MAX_STRING_LEN];
  char verb[PLUGIN_MAX_STRING_LEN];

  bool expects_response;
  uint32_t timeout_ms;

  uint8_t param_count;

  struct {
    char name[PLUGIN_MAX_STRING_LEN];
    IPCParamValue value;
  } params[PLUGIN_MAX_PARAMS];

  uint64_t sync_token;
};
struct IPCResponse {
  char command_id[PLUGIN_MAX_STRING_LEN];
  char instrument_name[PLUGIN_MAX_STRING_LEN];

  bool success;
  int32_t error_code;
  char error_message[PLUGIN_MAX_STRING_LEN];
  char text_response[PLUGIN_MAX_PAYLOAD];

  bool has_return_value;
  IPCParamValue return_value;

  bool has_large_data;
  char buffer_id[PLUGIN_MAX_STRING_LEN];
  uint32_t element_count;
  char data_type[32];
};
struct IPCBufferAck {
  char buffer_id[PLUGIN_MAX_STRING_LEN];
};
/// IPC message types
struct INSTRUMENT_SERVER_API IPCMessage {
  enum class Type : uint8_t {
    COMMAND = 1,
    RESPONSE = 2,
    HEARTBEAT = 3,
    SHUTDOWN = 4,
    SYNC_ACK = 5,      // Worker -> Server:  "I finished sync command"
    SYNC_CONTINUE = 6, // Server -> Worker: "All workers ready, proceed"
    BUFFER_ACK = 7     // Server -> Worker: "I own the buffer, you can release"
  };

  Type type{};
  uint64_t id{};         // Message/command ID
  uint64_t sync_token{}; // For synchronization across instruments

  union {
    IPCCommand command;
    IPCResponse response;
    IPCBufferAck buffer_ack;
  };

  IPCMessage() { std::memset(this, 0, sizeof(IPCMessage)); }
};

} // namespace instserver::ipc
