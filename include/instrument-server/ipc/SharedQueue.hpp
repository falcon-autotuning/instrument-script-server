#pragma once
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <chrono>
#include <optional>
#include <string>

namespace instserver {
namespace ipc {

/// Message structure for IPC communication
struct IPCMessage {
  enum class Type : uint32_t {
    COMMAND = 1,
    RESPONSE = 2,
    SHUTDOWN = 3,
    HEARTBEAT = 4,
    ERROR = 5
  };

  Type type;
  uint64_t id;           // Message ID for request/response matching
  uint32_t payload_size; // Size of serialized data
  char payload[8192];    // Serialized command/response (JSON or msgpack)

  static constexpr size_t MAX_PAYLOAD_SIZE = sizeof(payload);
};

/// Bidirectional IPC queue pair (request + response queues)
class SharedQueue {
public:
  SharedQueue(std::unique_ptr<boost::interprocess::message_queue> req_queue,
              std::unique_ptr<boost::interprocess::message_queue> resp_queue,
              const std::string &req_name, const std::string &resp_name);

  /// Create/open queue for server (creates both queues)
  static std::unique_ptr<SharedQueue>
  create_server_queue(const std::string &instrument_name);

  /// Create/open queue for worker (opens existing queues)
  static std::unique_ptr<SharedQueue>
  create_worker_queue(const std::string &instrument_name);

  /// Close and cleanup queues
  ~SharedQueue();

  /// Send message (non-blocking with timeout)
  bool
  send(const IPCMessage &msg,
       std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));

  /// Receive message (blocking with timeout)
  std::optional<IPCMessage>
  receive(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

  /// Check if queue is valid
  bool is_valid() const {
    return request_queue_ != nullptr && response_queue_ != nullptr;
  }

  /// Get queue names
  std::string get_request_queue_name() const { return request_queue_name_; }
  std::string get_response_queue_name() const { return response_queue_name_; }

  /// Cleanup queue files (call when instrument removed)
  static void cleanup(const std::string &instrument_name);

private:
  std::unique_ptr<boost::interprocess::message_queue> request_queue_;
  std::unique_ptr<boost::interprocess::message_queue> response_queue_;
  std::string request_queue_name_;
  std::string response_queue_name_;
};

} // namespace ipc
} // namespace instserver
