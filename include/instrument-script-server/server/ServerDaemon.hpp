// ServerDaemon.hpp
#pragma once
#include "instrument-script-server/export.h"

#include "instrument-script-server/server/InstrumentRegistry.hpp"
#include "instrument-script-server/server/SyncCoordinator.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace instserver {

/// Forward-declare HttpRpcServer
namespace server {
class HttpRpcServer;
}

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

  /// Get the PID file path
  static std::string get_pid_file_path();

  /// Get the shutdown pipe path
  static std::string get_shutdown_pipe_path();

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
  bool create_pid_file();
  void remove_pid_file();
  bool create_shutdown_pipe();
  void close_shutdown_pipe();
  void signal_shutdown_pipe();

  // running_ is atomic for the hot path polling (daemon_loop)
  std::atomic<bool> running_{false};
  std::unique_ptr<std::thread> daemon_thread_;
  std::unique_ptr<std::thread> shutdown_listener_thread_;
  std::mutex mutex_;

  std::unique_ptr<SyncCoordinator> sync_coordinator_;

  // RPC listener
  server::HttpRpcServer *rpc_server_{nullptr};
  uint16_t rpc_port_{0};

  // Shutdown pipe handles (platform-specific)
#ifdef _WIN32
  HANDLE shutdown_pipe_{INVALID_HANDLE_VALUE};
#else
  int shutdown_pipe_fd_{-1};
#endif
};

} // namespace instserver
