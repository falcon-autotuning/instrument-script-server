#pragma once
#include "instrument-server/server/InstrumentRegistry.hpp"
#include "instrument-server/server/SyncCoordinator.hpp"
#include <atomic>
#include <string>
#include <thread>

namespace instserver {

/// Server daemon that manages instrument registry and accepts commands
class ServerDaemon {
public:
  static ServerDaemon &instance();

  /// Start the daemon (returns immediately, runs in background)
  bool start();

  /// Stop the daemon
  void stop();

  /// Check if daemon is running
  bool is_running() const;

  /// Get the PID file path
  static std::string get_pid_file_path();

  /// Get the lock file path
  static std::string get_lock_file_path();

  /// Check if another instance is running
  static bool is_already_running();

  /// Get daemon PID if running
  static int get_daemon_pid();

private:
  ServerDaemon() = default;
  ~ServerDaemon();

  ServerDaemon(const ServerDaemon &) = delete;
  ServerDaemon &operator=(const ServerDaemon &) = delete;

  void daemon_loop();
  bool create_pid_file();
  void remove_pid_file();

  std::atomic<bool> running_{false};
  std::thread daemon_thread_;
  InstrumentRegistry *registry_{nullptr};
  SyncCoordinator *sync_coordinator_{nullptr};
};

} // namespace instserver
