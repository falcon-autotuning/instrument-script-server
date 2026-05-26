#include "instrument-script-server/ipc/DataBufferManager.hpp"
#include "instrument-script-server/Logger.hpp"
#include <instrument-data.h>
#include <chrono>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace instserver::ipc {

static inline ArrayType map_data_type(DataType type) {
  return static_cast<ArrayType>(type);
}

static inline DataType map_array_type(ArrayType type) {
  return static_cast<DataType>(type);
}

// DataBuffer implementation

DataBuffer::DataBuffer(const std::string &buffer_id, void *data,
                       size_t byte_size, DataType data_type,
                       size_t element_count, void *c_buf)
    : buffer_id_(buffer_id), data_(data), byte_size_(byte_size),
      element_count_(element_count), data_type_(data_type),
      owns_memory_(true), c_buf_(c_buf) {
}

DataBuffer::~DataBuffer() {
  if (owns_memory_ && !buffer_id_.empty()) {
    LOG_DEBUG("DATA_BUFFER", "DESTRUCT", "Releasing shared handle for buffer {}", buffer_id_);
    data_manager_release_buffer(buffer_id_.c_str());
  } else if (!owns_memory_ && c_buf_) {
    // Non-owning peek: balance the process-local ref count in the C library
    data_buffer_unref(static_cast<::DataBuffer*>(c_buf_));
  }
}

DataBuffer::DataBuffer(DataBuffer &&other) noexcept
    : buffer_id_(std::move(other.buffer_id_)), data_(other.data_),
      byte_size_(other.byte_size_), element_count_(other.element_count_),
      data_type_(other.data_type_), owns_memory_(other.owns_memory_),
      c_buf_(other.c_buf_) {
  other.data_ = nullptr;
  other.buffer_id_.clear();
  other.owns_memory_ = false;
  other.c_buf_ = nullptr;
}

DataBuffer &DataBuffer::operator=(DataBuffer &&other) noexcept {
  if (this != &other) {
    if (owns_memory_ && !buffer_id_.empty()) {
      data_manager_release_buffer(buffer_id_.c_str());
    } else if (!owns_memory_ && c_buf_) {
      data_buffer_unref(static_cast<::DataBuffer*>(c_buf_));
    }

    buffer_id_ = std::move(other.buffer_id_);
    data_ = other.data_;
    byte_size_ = other.byte_size_;
    element_count_ = other.element_count_;
    data_type_ = other.data_type_;
    owns_memory_ = other.owns_memory_;
    c_buf_ = other.c_buf_;

    other.data_ = nullptr;
    other.buffer_id_.clear();
    other.owns_memory_ = false;
    other.c_buf_ = nullptr;
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
    const auto *arr = static_cast<const float *>(data_);
    for (size_t i = 0; i < element_count_; ++i) {
      file << arr[i] << "\n";
    }
    break;
  }
  case DataType::FLOAT64: {
    const auto *arr = static_cast<const double *>(data_);
    for (size_t i = 0; i < element_count_; ++i) {
      file << arr[i] << "\n";
    }
    break;
  }
  case DataType::INT32: {
    const auto *arr = static_cast<const int32_t *>(data_);
    for (size_t i = 0; i < element_count_; ++i) {
      file << arr[i] << "\n";
    }
    break;
  }
  case DataType::INT64: {
    const auto *arr = static_cast<const int64_t *>(data_);
    for (size_t i = 0; i < element_count_; ++i) {
      file << arr[i] << "\n";
    }
    break;
  }
  case DataType::UINT32: {
    const auto *arr = static_cast<const uint32_t *>(data_);
    for (size_t i = 0; i < element_count_; ++i) {
      file << arr[i] << "\n";
    }
    break;
  }
  case DataType::UINT64: {
    const auto *arr = static_cast<const uint64_t *>(data_);
    for (size_t i = 0; i < element_count_; ++i) {
      file << arr[i] << "\n";
    }
    break;
  }
  case DataType::UINT8: {
    const auto *arr = static_cast<const uint8_t *>(data_);
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

  void *raw_data = nullptr;
  gchar *c_id = data_manager_create_buffer_zero_copy(
      instrument_name.c_str(),
      command_id.c_str(),
      map_data_type(data_type),
      element_count,
      &raw_data
  );
  if (c_id == nullptr) {
    LOG_ERROR("DATA_BUFFER", "CREATE", "Failed to create shared memory buffer via instrument-data");
    return "";
  }

  std::string buffer_id(c_id);
  g_free(c_id);

  if (data != nullptr && raw_data != nullptr) {
    std::memcpy(raw_data, data, element_count * element_size);
  }

  size_t bytes = element_count * element_size;
  LOG_INFO("DATA_BUFFER", "CREATE",
           "Created shared buffer {} for {}. {} ({} elements, {} bytes)", buffer_id,
           instrument_name, command_id, element_count, bytes);

  return buffer_id;
}

std::string DataBufferManager::create_buffer_with_metadata(
    const DataBufferMetadata &metadata, const void *data) {
  return create_buffer(metadata.instrument_name, metadata.command_id,
                       metadata.data_type, metadata.element_count, data);
}

std::shared_ptr<DataBuffer>
DataBufferManager::get_buffer(const std::string &buffer_id) {
  SharedMetadata c_meta;
  if (data_manager_get_metadata(buffer_id.c_str(), &c_meta) == FALSE) {
    return nullptr;
  }

  ::DataBuffer *c_buf = data_manager_get_buffer(buffer_id.c_str());
  if (c_buf == nullptr) {
    return nullptr;
  }

  void *raw_data = data_buffer_data(c_buf);
  DataType dtype = map_array_type(c_meta.type);
  size_t bytes = c_meta.byte_size;
  size_t element_count = c_meta.element_count;

  // Non-owning wrapper: lifetime is managed by the caller + release_buffer().
  auto buffer = std::make_shared<DataBuffer>(buffer_id, raw_data, bytes, dtype, element_count, c_buf);
  buffer->set_non_owning();
  return buffer;
}

std::optional<DataBufferMetadata>
DataBufferManager::get_metadata(const std::string &buffer_id) const {
  SharedMetadata c_meta;
  if (data_manager_get_metadata(buffer_id.c_str(), &c_meta) == FALSE) {
    return std::nullopt;
  }

  DataBufferMetadata metadata;
  metadata.buffer_id = c_meta.buffer_id;
  metadata.instrument_name = c_meta.instrument_name;
  metadata.command_id = c_meta.command_id;
  metadata.data_type = map_array_type(c_meta.type);
  metadata.element_count = c_meta.element_count;
  metadata.byte_size = c_meta.byte_size;
  metadata.timestamp_ms = c_meta.timestamp_ms;
  metadata.description = "";
  metadata.dimensions = {c_meta.element_count};

  return metadata;
}

void DataBufferManager::release_buffer(const std::string &buffer_id) {
  data_manager_release_buffer(buffer_id.c_str());
}

std::vector<std::string> DataBufferManager::list_buffers() const {
  size_t count = 0;
  gchar **list = data_manager_list_buffers(&count);
  std::vector<std::string> result;
  if (list) {
    for (size_t i = 0; i < count; ++i) {
      if (list[i]) {
        result.push_back(list[i]);
        g_free(list[i]);
      }
    }
    g_free(list);
  }
  return result;
}

size_t DataBufferManager::total_memory_usage() const {
  return data_manager_total_local_memory();
}

void DataBufferManager::clear_all() {
  size_t count = 0;
  gchar **list = data_manager_list_buffers(&count);
  if (list) {
    for (size_t i = 0; i < count; ++i) {
      if (list[i]) {
        data_manager_release_buffer(list[i]);
        g_free(list[i]);
      }
    }
    g_free(list);
  }
}

bool DataBufferManager::add_offset(const std::string &buffer_id,
                                   double offset) {
  return data_manager_add_offset(buffer_id.c_str(), offset) == TRUE;
}

bool DataBufferManager::multiply_gain(const std::string &buffer_id,
                                      double gain) {
  return data_manager_multiply_gain(buffer_id.c_str(), gain) == TRUE;
}

} // namespace instserver::ipc
