#pragma once

#include "instrument-script-server/export.h"
#include <cstddef>
#include <cstdint>
#include <instrument-plugin.h>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace instserver::ipc {

/// Metadata about a data buffer
struct INSTRUMENT_SERVER_API DataBufferMetadata {
  std::string buffer_id;       // Unique identifier
  std::string instrument_name; // Source instrument
  std::string command_id;      // Command that generated this data
  ArrayType data_type;         // Type of data
  size_t element_count;        // Number of elements
  size_t byte_size;            // Total size in bytes
  uint64_t timestamp_ms;       // When data was captured
  std::string description;     // Optional description

  // For multi-dimensional data
  std::vector<size_t> dimensions; // e.g., [1024, 512] for 2D array
};

/// Manages shared memory buffers for large data transfers
class INSTRUMENT_SERVER_API DataBufferManager {
public:
  static DataBufferManager &instance();

  /// Get buffer by ID
  void save_buffer(const std::string &buffer_id);

  /// Get buffer metadata
  std::optional<DataBufferMetadata>
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

} // namespace instserver::ipc
