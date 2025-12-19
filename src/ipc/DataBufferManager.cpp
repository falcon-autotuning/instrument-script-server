#include "instrument-server/ipc/DataBufferManager.hpp"
#include "instrument-server/Logger.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>

namespace instserver {
namespace ipc {

// DataBuffer implementation

DataBuffer::DataBuffer(const std::string &buffer_id, void *data,
                       size_t byte_size, DataType data_type,
                       size_t element_count)
    : buffer_id_(buffer_id), data_(data), byte_size_(byte_size),
      element_count_(element_count), data_type_(data_type), owns_memory_(true) {
}

DataBuffer::~DataBuffer() {
  if (owns_memory_ && data_) {
    free(data_);
    data_ = nullptr;
  }
}

DataBuffer::DataBuffer(DataBuffer &&other) noexcept
    : buffer_id_(std::move(other.buffer_id_)), data_(other.data_),
      byte_size_(other.byte_size_), element_count_(other.element_count_),
      data_type_(other.data_type_), owns_memory_(other.owns_memory_) {
  other.data_ = nullptr;
  other.owns_memory_ = false;
}

DataBuffer &DataBuffer::operator=(DataBuffer &&other) noexcept {
  if (this != &other) {
    if (owns_memory_ && data_) {
      free(data_);
    }

    buffer_id_ = std::move(other.buffer_id_);
    data_ = other.data_;
    byte_size_ = other.byte_size_;
    element_count_ = other.element_count_;
    data_type_ = other.data_type_;
    owns_memory_ = other.owns_memory_;

    other.data_ = nullptr;
    other.owns_memory_ = false;
  }
  return *this;
}

float *DataBuffer::as_float32() {
  if (data_type_ == DataType::FLOAT32) {
    return static_cast<float *>(data_);
  }
  return nullptr;
}

double *DataBuffer::as_float64() {
  if (data_type_ == DataType::FLOAT64) {
    return static_cast<double *>(data_);
  }
  return nullptr;
}

int32_t *DataBuffer::as_int32() {
  if (data_type_ == DataType::INT32) {
    return static_cast<int32_t *>(data_);
  }
  return nullptr;
}

int64_t *DataBuffer::as_int64() {
  if (data_type_ == DataType::INT64) {
    return static_cast<int64_t *>(data_);
  }
  return nullptr;
}

uint32_t *DataBuffer::as_uint32() {
  if (data_type_ == DataType::UINT32) {
    return static_cast<uint32_t *>(data_);
  }
  return nullptr;
}

uint64_t *DataBuffer::as_uint64() {
  if (data_type_ == DataType::UINT64) {
    return static_cast<uint64_t *>(data_);
  }
  return nullptr;
}

uint8_t *DataBuffer::as_uint8() {
  if (data_type_ == DataType::UINT8) {
    return static_cast<uint8_t *>(data_);
  }
  return nullptr;
}

const float *DataBuffer::as_float32() const {
  if (data_type_ == DataType::FLOAT32) {
    return static_cast<const float *>(data_);
  }
  return nullptr;
}

const double *DataBuffer::as_float64() const {
  if (data_type_ == DataType::FLOAT64) {
    return static_cast<const double *>(data_);
  }
  return nullptr;
}

const int32_t *DataBuffer::as_int32() const {
  if (data_type_ == DataType::INT32) {
    return static_cast<const int32_t *>(data_);
  }
  return nullptr;
}

const int64_t *DataBuffer::as_int64() const {
  if (data_type_ == DataType::INT64) {
    return static_cast<const int64_t *>(data_);
  }
  return nullptr;
}

bool DataBuffer::export_to_file(const std::string &filepath) const {
  std::ofstream file(filepath, std::ios::binary);
  if (!file) {
    return false;
  }

  file.write(static_cast<const char *>(data_), byte_size_);
  return file.good();
}

bool DataBuffer::export_to_csv(const std::string &filepath) const {
  std::ofstream file(filepath);
  if (!file) {
    return false;
  }

  switch (data_type_) {
  case DataType::FLOAT32: {
    const float *arr = static_cast<const float *>(data_);
    for (size_t i = 0; i < element_count_; ++i) {
      file << arr[i] << "\n";
    }
    break;
  }
  case DataType::FLOAT64: {
    const double *arr = static_cast<const double *>(data_);
    for (size_t i = 0; i < element_count_; ++i) {
      file << arr[i] << "\n";
    }
    break;
  }
  case DataType::INT32: {
    const int32_t *arr = static_cast<const int32_t *>(data_);
    for (size_t i = 0; i < element_count_; ++i) {
      file << arr[i] << "\n";
    }
    break;
  }
  case DataType::INT64: {
    const int64_t *arr = static_cast<const int64_t *>(data_);
    for (size_t i = 0; i < element_count_; ++i) {
      file << arr[i] << "\n";
    }
    break;
  }
  case DataType::UINT32: {
    const uint32_t *arr = static_cast<const uint32_t *>(data_);
    for (size_t i = 0; i < element_count_; ++i) {
      file << arr[i] << "\n";
    }
    break;
  }
  case DataType::UINT64: {
    const uint64_t *arr = static_cast<const uint64_t *>(data_);
    for (size_t i = 0; i < element_count_; ++i) {
      file << arr[i] << "\n";
    }
    break;
  }
  case DataType::UINT8: {
    const uint8_t *arr = static_cast<const uint8_t *>(data_);
    for (size_t i = 0; i < element_count_; ++i) {
      file << static_cast<int>(arr[i]) << "\n";
    }
    break;
  }
  default:
    return false;
  }

  return file.good();
}

// DataBufferManager implementation

DataBufferManager &DataBufferManager::instance() {
  static DataBufferManager manager;
  return manager;
}

std::string DataBufferManager::generate_buffer_id() {
  uint64_t id = next_buffer_id_.fetch_add(1);
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count();

  std::ostringstream oss;
  oss << "buffer_" << ms << "_" << id;
  return oss.str();
}

std::string DataBufferManager::create_buffer(const std::string &instrument_name,
                                             const std::string &command_id,
                                             DataType data_type,
                                             size_t element_count,
                                             const void *data) {
  size_t element_size = data_type_size(data_type);
  if (element_size == 0) {
    LOG_ERROR("DATA_BUFFER", "CREATE", "Invalid data type");
    return "";
  }

  size_t byte_size = element_count * element_size;

  // Allocate memory
  void *buffer_data = malloc(byte_size);
  if (!buffer_data) {
    LOG_ERROR("DATA_BUFFER", "CREATE", "Failed to allocate {} bytes",
              byte_size);
    return "";
  }

  // Copy data if provided
  if (data) {
    memcpy(buffer_data, data, byte_size);
  } else {
    memset(buffer_data, 0, byte_size);
  }

  std::string buffer_id = generate_buffer_id();

  auto buffer = std::make_shared<DataBuffer>(buffer_id, buffer_data, byte_size,
                                             data_type, element_count);

  DataBufferMetadata metadata;
  metadata.buffer_id = buffer_id;
  metadata.instrument_name = instrument_name;
  metadata.command_id = command_id;
  metadata.data_type = data_type;
  metadata.element_count = element_count;
  metadata.byte_size = byte_size;
  metadata.timestamp_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();

  std::lock_guard lock(mutex_);

  // Use try_emplace to construct BufferEntry in-place
  buffers_.try_emplace(buffer_id, buffer, std::move(metadata), 1);

  LOG_INFO("DATA_BUFFER", "CREATE",
           "Created buffer {} for {}. {} ({} elements, {} bytes)", buffer_id,
           instrument_name, command_id, element_count, byte_size);

  return buffer_id;
}

std::string DataBufferManager::create_buffer_with_metadata(
    const DataBufferMetadata &metadata, const void *data) {
  return create_buffer(metadata.instrument_name, metadata.command_id,
                       metadata.data_type, metadata.element_count, data);
}

std::shared_ptr<DataBuffer>
DataBufferManager::get_buffer(const std::string &buffer_id) {
  std::lock_guard lock(mutex_);
  auto it = buffers_.find(buffer_id);
  if (it == buffers_.end()) {
    return nullptr;
  }

  it->second.ref_count++;
  return it->second.buffer;
}

std::optional<DataBufferMetadata>
DataBufferManager::get_metadata(const std::string &buffer_id) const {
  std::lock_guard lock(mutex_);
  auto it = buffers_.find(buffer_id);
  if (it == buffers_.end()) {
    return std::nullopt;
  }
  return it->second.metadata;
}

void DataBufferManager::release_buffer(const std::string &buffer_id) {
  std::lock_guard lock(mutex_);
  auto it = buffers_.find(buffer_id);
  if (it == buffers_.end()) {
    return;
  }

  uint32_t ref_count = --it->second.ref_count;
  LOG_DEBUG("DATA_BUFFER", "RELEASE", "Buffer {} ref count now {}", buffer_id,
            ref_count);

  if (ref_count == 0) {
    LOG_INFO("DATA_BUFFER", "RELEASE", "Releasing buffer {}", buffer_id);
    buffers_.erase(it);
  }
}

std::vector<std::string> DataBufferManager::list_buffers() const {
  std::lock_guard lock(mutex_);
  std::vector<std::string> ids;
  ids.reserve(buffers_.size());
  for (const auto &[id, _] : buffers_) {
    ids.push_back(id);
  }
  return ids;
}

size_t DataBufferManager::total_memory_usage() const {
  std::lock_guard lock(mutex_);
  size_t total = 0;
  for (const auto &[_, entry] : buffers_) {
    total += entry.metadata.byte_size;
  }
  return total;
}

void DataBufferManager::clear_all() {
  std::lock_guard lock(mutex_);
  LOG_INFO("DATA_BUFFER", "CLEAR", "Clearing {} buffers", buffers_.size());
  buffers_.clear();
}

} // namespace ipc
} // namespace instserver
