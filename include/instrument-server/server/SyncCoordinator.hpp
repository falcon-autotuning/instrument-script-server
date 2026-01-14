#pragma once
#include "instrument-server/export.h"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace instserver {

/// Coordinates synchronization barriers across multiple instruments
/// for parallel execution blocks
class INSTRUMENT_SERVER_API SyncCoordinator {
public:
  /// Register a new sync barrier with the instruments that must participate
  void register_barrier(uint64_t sync_token,
                        const std::vector<std::string> &instruments);

  /// Called when an instrument acknowledges completion of a sync command
  /// Returns true if all instruments have acknowledged (barrier complete)
  bool handle_ack(uint64_t sync_token, const std::string &instrument_name);

  /// Get all instruments waiting on this barrier
  std::vector<std::string> get_waiting_instruments(uint64_t sync_token) const;

  /// Check if a barrier exists
  bool has_barrier(uint64_t sync_token) const;

  /// Remove a completed barrier
  void clear_barrier(uint64_t sync_token);

  /// Get count of active barriers
  size_t active_barrier_count() const;

private:
  struct SyncBarrier {
    std::set<std::string> expected_instruments;
    std::set<std::string> acked_instruments;
    std::chrono::steady_clock::time_point created_at;
  };

  mutable std::mutex mutex_;
  std::map<uint64_t, SyncBarrier> barriers_;
};

} // namespace instserver
