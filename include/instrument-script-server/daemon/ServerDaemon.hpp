// ServerDaemon.hpp
#pragma once
#include "instrument-script-server/export.h"

#include "instrument-script-server/daemon/SyncCoordinator.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace instserver {

/// Forward-declare GrpcServer
namespace daemon {
class GrpcServer;
}
// Gets the shutdown pipe for the server daemon process
std::string INSTRUMENT_SERVER_API get_shutdown_pipe_path();
bool INSTRUMENT_SERVER_API create_shutdown_pipe();
void INSTRUMENT_SERVER_API close_shutdown_pipe();
void INSTRUMENT_SERVER_API remove_pid_file();
std::string INSTRUMENT_SERVER_API get_pid_file_path();
bool INSTRUMENT_SERVER_API create_pid_file();
void INSTRUMENT_SERVER_API signal_shutdown_pipe();

/// Server daemon that manages instrument registry and accepts commands
class INSTRUMENT_SERVER_API ServerDaemon {
public:
  static ServerDaemon &instance();

  /// Start the daemon (returns immediately, runs in background)
  bool start();

  /// Stop the daemon (graceful shutdown via pipe/socket)
  void stop();

  /// Check if daemon is running
  [[nodiscard]] bool is_running() const;

  /// Set RPC port to bind the HttpRpcServer (0 = disabled / ephemeral)
  void set_rpc_port(uint16_t port) { rpc_port_ = port; }

  /// Get the configured RPC port (0 if not set)
  [[nodiscard]] uint16_t rpc_port() const { return rpc_port_; }

  /// Check if another instance is running
  static bool is_already_running();

  /// Get daemon PID if running
  static int get_daemon_pid();

  SyncCoordinator &sync_coordinator() { return *sync_coordinator_; }

private:
  ServerDaemon() = default;
  ~ServerDaemon();

  ServerDaemon(const ServerDaemon &) = delete;
  ServerDaemon &operator=(const ServerDaemon &) = delete;

  void daemon_loop();
  void shutdown_listener_loop();

  // running_ is atomic for the hot path polling (daemon_loop)
  std::atomic<bool> running_{false};
  std::unique_ptr<std::thread> daemon_thread_;
  std::unique_ptr<std::thread> shutdown_listener_thread_;
  std::mutex mutex_;

  std::unique_ptr<SyncCoordinator> sync_coordinator_;

  // RPC listener
  daemon::GrpcServer *rpc_server_{nullptr};
  uint16_t rpc_port_{0};
};

} // namespace instserver
