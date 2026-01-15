#pragma once

#include <atomic>
#include <string>
#include <thread>

namespace instserver {
namespace server {

class HttpRpcServer {
public:
  HttpRpcServer();
  ~HttpRpcServer();

  // Start server on loopback with given port. Returns true if started.
  bool start(uint16_t port);

  // Stop server and join thread.
  void stop();

  // Get port (useful if started with 0 to pick ephemeral port)
  uint16_t port() const;

private:
  void run_loop(uint16_t port);

  std::atomic<bool> running_;
  std::thread server_thread_;
  uint16_t bound_port_;
};

} // namespace server
} // namespace instserver
