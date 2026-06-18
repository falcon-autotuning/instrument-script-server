#pragma once
#include "instrument-script-server/export.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <instrument-plugin.h>

namespace instserver::ipc {

/**
 * @file IPCMessage.hpp
 * @brief IPC message format for communication between server and worker
 * processes.
 *
 * This IPC system is built on fixed-size message queues. To support
 * variable-sized data (such as parameter lists or return values), messages may
 * be split into multiple "chunks".
 *
 * Each IPCMessage can carry up to PARAM_CHUNK parameters or return
 * values. If more data is required, multiple IPCMessage instances are sent
 * sequentially.
 *
 * The receiver is responsible for reassembling the full dataset using:
 * - param_total / return_total (total number of elements)
 * - param_index / return_index (starting index of this chunk)
 *
 * This design avoids dynamic allocation while maintaining flexibility.
 */

// Tuned so sizeof(IPCMessage) ≈ 2KB for optimal IPC/cache behavior
constexpr size_t PARAM_CHUNK = 6;

/**
 * @brief Command sent from server to worker.
 *
 * Supports chunked parameter transmission for large parameter sets.
 */
struct INSTRUMENT_SERVER_API IPCCommand {
  /// Command string (e.g. SCPI command)
  std::array<char, PLUGIN_MAX_STRING_LEN> command;

  /// Command timeout in milliseconds
  uint32_t timeout_ms;
  /**
   * @brief Number of parameters in this message chunk.
   *
   * Must be <= PARAM_CHUNK.
   */
  uint8_t param_count;

  /**
   * @brief Total number of parameters for the full command.
   *
   * If param_total > param_count, the receiver must expect
   * additional IPCCommand messages with the same id.
   */
  uint16_t param_total;

  /**
   * @brief Parameter data for this chunk.
   */
  std::array<Variable, PARAM_CHUNK> params;
};

/**
 * @brief Response returned from worker to server.
 *
 * Supports chunked return values using the same scheme as IPCCommand.
 */
struct IPCResponse {

  /// Error code (0 = success)
  int8_t error_code;

  /**
   * @brief Number of return values in this chunk.
   */
  uint8_t return_count;

  /**
   * @brief Total number of return values.
   *
   * If return_total > return_count, multiple messages are expected.
   */
  uint16_t return_total;

  /**
   * @brief Return values for this chunk.
   */
  std::array<Variable, PARAM_CHUNK> returns;
};

/**
 * @brief Acknowledges ownership transfer of a buffer.
 */
struct IPCBufferAck {
  std::array<char, PLUGIN_MAX_STRING_LEN> buffer_id;
};

/**
 * @brief Top-level IPC message wrapper.
 *
 * Messages are sent over fixed-size queues. Payload is selected via `type`.
 */
struct INSTRUMENT_SERVER_API IPCMessage {

  /**
   * @brief Message type.
   */
  enum class Type : uint8_t {
    COMMAND = 1,
    RESPONSE = 2,
    HEARTBEAT = 3,
    SHUTDOWN = 4,
    SYNC_ACK = 5,
    SYNC_CONTINUE = 6,
    BUFFER_ACK = 7
  };

  /// Message type discriminator
  Type type{};

  /// Synchronization token shared across related messages
  uint64_t sync_token{};

  /// Command identifier (must match IPCCommand.id)
  std::array<char, PLUGIN_MAX_STRING_LEN> id{};

  /**
   * @brief Tagged union containing message payload.
   *
   * Only the field corresponding to `type` is valid.
   */
  union {
    IPCCommand command{};
    IPCResponse response;
    IPCBufferAck buffer_ack;
  };

  /**
   * @brief Zero-initialize message.
   *
   * Safe because all members are POD types.
   */
  IPCMessage() = default;
};

} // namespace instserver::ipc
