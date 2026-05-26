#pragma once

#include "instrument-script-server/export.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace instserver::ipc {

/// Type of data stored in buffer
enum class DataType : uint8_t {
  FLOAT32 = 0,
  FLOAT64 = 1,
  INT32 = 2,
  INT64 = 3,
  UINT32 = 4,
  UINT64 = 5,
  UINT8 = 6
};

/// Convert DataType to string
inline const char *data_type_to_string(DataType type) {
  switch (type) {
  case DataType::FLOAT32:
    return "float32";
  case DataType::FLOAT64:
    return "float64";
  case DataType::INT32:
    return "int32";
  case DataType::INT64:
    return "int64";
  case DataType::UINT32:
    return "uint32";
  case DataType::UINT64:
    return "uint64";
  case DataType::UINT8:
    return "uint8";
  default:
    return "unknown";
  }
}

/// Get size of data type in bytes
inline size_t data_type_size(DataType type) {
  switch (type) {
  case DataType::FLOAT32:
    return sizeof(float);
  case DataType::FLOAT64:
    return sizeof(double);
  case DataType::INT32:
  case DataType::UINT32:
    return sizeof(int32_t);
  case DataType::INT64:
  case DataType::UINT64:
    return sizeof(int64_t);
  case DataType::UINT8:
    return sizeof(uint8_t);
  default:
    return 0;
  }
}

/// Metadata about a data buffer
struct INSTRUMENT_SERVER_API DataBufferMetadata {
  std::string buffer_id;       // Unique identifier
  std::string instrument_name; // Source instrument
  std::string command_id;      // Command that generated this data
  DataType data_type;          // Type of data
  size_t element_count;        // Number of elements
  size_t byte_size;            // Total size in bytes
  uint64_t timestamp_ms;       // When data was captured
  std::string description;     // Optional description

  // For multi-dimensional data
  std::vector<size_t> dimensions; // e.g., [1024, 512] for 2D array
};

/// Handle to shared memory data buffer
class INSTRUMENT_SERVER_API DataBuffer {
public:
  DataBuffer(const std::string &buffer_id, void *data, size_t byte_size,
             DataType data_type, size_t element_count, void *c_buf = nullptr);
  ~DataBuffer();

  // Non-copyable, movable
  DataBuffer(const DataBuffer &) = delete;
  DataBuffer &operator=(const DataBuffer &) = delete;
  DataBuffer(DataBuffer &&other) noexcept;
  DataBuffer &operator=(DataBuffer &&other) noexcept;

  const std::string &id() const { return buffer_id_; }
  void *data() { return data_; }
  const void *data() const { return data_; }
  size_t byte_size() const { return byte_size_; }
  size_t element_count() const { return element_count_; }
  DataType data_type() const { return data_type_; }

  // Type-safe accessors
  float *as_float32();
  double *as_float64();
  int32_t *as_int32();
  int64_t *as_int64();
  uint32_t *as_uint32();
  uint64_t *as_uint64();
  uint8_t *as_uint8();

  /// Mark this wrapper as non-owning so the destructor will not call
  /// data_manager_release_buffer. Used by DataBufferManager::get_buffer()
  /// to return a peek without bumping the global ref count.
  void set_non_owning() { owns_memory_ = false; }

  const float *as_float32() const;
  const double *as_float64() const;
  const int32_t *as_int32() const;
  const int64_t *as_int64() const;

  // Export to file (for database consumption)
  bool export_to_file(const std::string &filepath) const;
  bool export_to_csv(const std::string &filepath) const;

private:
  std::string buffer_id_;
  void *data_;
  size_t byte_size_;
  size_t element_count_;
  DataType data_type_;
  bool owns_memory_;
  void *c_buf_;
};

/// Manages shared memory buffers for large data transfers
class INSTRUMENT_SERVER_API DataBufferManager {
public:
  static DataBufferManager &instance();

  /// Create a new buffer and return its ID
  std::string create_buffer(const std::string &instrument_name,
                            const std::string &command_id, DataType data_type,
                            size_t element_count, const void *data = nullptr);

  /// Create buffer with metadata
  std::string create_buffer_with_metadata(const DataBufferMetadata &metadata,
                                          const void *data = nullptr);

  /// Get buffer by ID
  std::shared_ptr<DataBuffer> get_buffer(const std::string &buffer_id);

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
};

} // namespace instserver::ipc
