#include "instrument-server/server/SyncCoordinator.hpp"
#include "instrument-server/Logger.hpp"

namespace instserver {

void SyncCoordinator::register_barrier(
    uint64_t sync_token, const std::vector<std::string> &instruments) {
  std::lock_guard lock(mutex_);

  SyncBarrier barrier;
  barrier.expected_instruments =
      std::set<std::string>(instruments.begin(), instruments.end());
  barrier.created_at = std::chrono::steady_clock::now();

  barriers_[sync_token] = std::move(barrier);

  LOG_DEBUG("SYNC", "REGISTER",
            "Registered barrier token={} with {} instruments", sync_token,
            instruments.size());
}

bool SyncCoordinator::handle_ack(uint64_t sync_token,
                                 const std::string &instrument_name) {
  std::lock_guard lock(mutex_);

  auto it = barriers_.find(sync_token);
  if (it == barriers_.end()) {
    LOG_WARN("SYNC", "ACK", "Unknown sync token:  {}", sync_token);
    return false;
  }

  auto &barrier = it->second;

  // Check if this instrument is expected
  if (barrier.expected_instruments.find(instrument_name) ==
      barrier.expected_instruments.end()) {
    LOG_WARN("SYNC", "ACK",
             "Unexpected ACK from {} for token {} (not in expected set)",
             instrument_name, sync_token);
    return false;
  }

  // Record acknowledgment
  barrier.acked_instruments.insert(instrument_name);

  LOG_DEBUG("SYNC", "ACK", "Instrument {} ACKed token {} ({}/{})",
            instrument_name, sync_token, barrier.acked_instruments.size(),
            barrier.expected_instruments.size());

  // Check if all instruments have acknowledged
  bool complete = (barrier.acked_instruments == barrier.expected_instruments);

  if (complete) {
    LOG_INFO("SYNC", "COMPLETE",
             "Barrier {} complete, all {} instruments ACKed", sync_token,
             barrier.expected_instruments.size());
    barriers_.erase(it);
    LOG_DEBUG("SYNC", "AUTO_CLEAR", "Auto-cleared completed barrier token={}",
              sync_token);
  }

  return complete;
}

std::vector<std::string>
SyncCoordinator::get_waiting_instruments(uint64_t sync_token) const {
  std::lock_guard lock(mutex_);

  auto it = barriers_.find(sync_token);
  if (it == barriers_.end()) {
    return {};
  }

  const auto &barrier = it->second;
  std::vector<std::string> waiting;

  for (const auto &inst : barrier.expected_instruments) {
    if (barrier.acked_instruments.find(inst) ==
        barrier.acked_instruments.end()) {
      waiting.push_back(inst);
    }
  }

  return waiting;
}

bool SyncCoordinator::has_barrier(uint64_t sync_token) const {
  std::lock_guard lock(mutex_);
  return barriers_.find(sync_token) != barriers_.end();
}

void SyncCoordinator::clear_barrier(uint64_t sync_token) {
  std::lock_guard lock(mutex_);
  barriers_.erase(sync_token);
  LOG_DEBUG("SYNC", "CLEAR", "Cleared barrier token={}", sync_token);
}

size_t SyncCoordinator::active_barrier_count() const {
  std::lock_guard lock(mutex_);
  return barriers_.size();
}

} // namespace instserver
