#pragma once

#include "instserver/daemon/v1/daemon_messages.pb.h"
#include <cstddef>
#include <instrument-data.h>
#include <instrument-plugin.h>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace instserver::daemon {

/// Manages shared memory buffers for large data transfers
class DataBufferManager {
public:
  static DataBufferManager &instance();

  /// Get buffer by ID
  void save_buffer(const std::string &buffer_id);

  /// Get buffer metadata
  std::optional<v1::DataBufferMetadata>
  get_metadata(const std::string &buffer_id) const;

  /// Release buffer (decrements ref count)
  void release_buffer(const std::string &buffer_id);

  /// List all active buffers
  std::vector<std::string> list_buffers() const;

  /// Get total memory usage
  size_t total_memory_usage() const;

  /// Clear all buffers (for cleanup)
  void clear_all();

  /// Apply offset to all elements in a buffer (array math operation)
  /// Returns true if successful, false if buffer not found or wrong type
  bool add_offset(const std::string &buffer_id, double offset);

  /// Apply gain (multiply) to all elements in a buffer (array math operation)
  /// Returns true if successful, false if buffer not found or wrong type
  bool multiply_gain(const std::string &buffer_id, double gain);

private:
  DataBufferManager() = default;
  bool contains_buffer_unsafe(const std::string &buffer_id) const;

  mutable std::mutex mutex_;
  std::list<std::pair<std::string, size_t>> active_buffers_;
};

} // namespace instserver::daemon
