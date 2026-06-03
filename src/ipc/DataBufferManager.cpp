#include "instrument-script-server/ipc/DataBufferManager.hpp"
#include <cstring>
#include <instrument-data.h>
#include <instrument-log/inst_logging.h>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace instserver::ipc {

DataBufferManager &DataBufferManager::instance() {
  static DataBufferManager manager;
  return manager;
}

void DataBufferManager::save_buffer(const std::string &buffer_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &pair : active_buffers_) {
    if (pair.first == buffer_id) {
      return;
    }
  }
  // Claim stake on buffer
  ::DataBuffer *c_buf = data_manager_get_buffer(buffer_id.c_str());
  if (c_buf == nullptr) {
    LOG_ERROR("DataBufferManager", buffer_id.c_str(), "No buffer at id\n");
    return;
  }
  SharedMetadata c_meta;
  if (!data_manager_get_metadata(buffer_id.c_str(), &c_meta)) {
    LOG_ERROR("DataBufferManager", buffer_id.c_str(),
              "No metadata found during save\n");
    return;
  }
  size_t bytes = c_meta.byte_size;
  active_buffers_.emplace_back(buffer_id, bytes);
}

std::optional<DataBufferMetadata>
DataBufferManager::get_metadata(const std::string &buffer_id) const {
  SharedMetadata c_meta;
  if (!data_manager_get_metadata(buffer_id.c_str(), &c_meta)) {
    LOG_ERROR("DataBufferManager", buffer_id.c_str(),
              "No metadata found at id\n");
    return std::nullopt;
  }

  DataBufferMetadata metadata;
  metadata.buffer_id = c_meta.buffer_id;
  metadata.instrument_name = c_meta.instrument_name;
  metadata.command_id = c_meta.command_id;
  metadata.data_type = c_meta.type;
  metadata.element_count = c_meta.element_count;
  metadata.byte_size = c_meta.byte_size;
  metadata.timestamp_ms = c_meta.timestamp_ms;
  metadata.description = "";
  metadata.dimensions = {c_meta.element_count};

  return metadata;
}

void DataBufferManager::release_buffer(const std::string &buffer_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = active_buffers_.begin(); it != active_buffers_.end(); ++it) {
    if (it->first == buffer_id) {
      active_buffers_.erase(it);
      data_manager_release_buffer(buffer_id.c_str());
      break;
    }
  }
}

std::vector<std::string> DataBufferManager::list_buffers() const {
  std::vector<std::string> result;
  std::lock_guard<std::mutex> lock(mutex_);
  result.reserve(active_buffers_.size());
  for (const auto &pair : active_buffers_) {
    result.push_back(pair.first);
  }
  return result;
}

size_t DataBufferManager::total_memory_usage() const {
  size_t total = 0;
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &pair : active_buffers_) {
    total += pair.second;
  }
  return total;
}

void DataBufferManager::clear_all() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &pair : active_buffers_) {
    data_manager_release_buffer(pair.first.c_str());
  }
  active_buffers_.clear();
}

bool DataBufferManager::add_offset(const std::string &buffer_id,
                                   double offset) {
  return data_manager_add_offset(buffer_id.c_str(), offset);
}

bool DataBufferManager::multiply_gain(const std::string &buffer_id,
                                      double gain) {
  return data_manager_multiply_gain(buffer_id.c_str(), gain);
}

} // namespace instserver::ipc
