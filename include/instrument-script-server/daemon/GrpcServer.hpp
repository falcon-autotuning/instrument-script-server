#pragma once

#include "instrument-script-server/export.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace grpc {
class Server;
}

namespace instserver::daemon {

class INSTRUMENT_SERVER_API GrpcServer {
public:
  GrpcServer();
  ~GrpcServer();

  // Start server on loopback with given port. Returns true if started.
  bool start(uint16_t port);

  // Stop server and join thread.
  void stop();

  // Get port (useful if started with 0 to pick ephemeral port)
  [[nodiscard]] uint16_t port() const;

private:
  void run_loop(uint16_t port);

  std::atomic<bool> running_{false};
  std::thread server_thread_;
  uint16_t bound_port_{0};
  std::unique_ptr<grpc::Server> server_;
};

} // namespace instserver::daemon
