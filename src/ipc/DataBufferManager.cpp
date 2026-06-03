#include "instrument-script-server/ipc/DataBufferManager.hpp"
#include <algorithm>
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

bool DataBufferManager::contains_buffer_unsafe(const std::string &buffer_id) {
  return std::any_of(
      active_buffers_.begin(), active_buffers_.end(),
      [&buffer_id](const auto &pair) { return pair.first == buffer_id; });
}
DataBufferManager &DataBufferManager::instance() {
  static DataBufferManager manager;
  return manager;
}

void DataBufferManager::save_buffer(const std::string &buffer_id) {
  // Step 1: Fast check under the lock to see if we already manage it
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (contains_buffer_unsafe(buffer_id)) {
      return;
    }
  }

  // Step 2: Fetch buffer and metadata
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

  // Step 3: Lock again briefly just to register the buffer
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (contains_buffer_unsafe(buffer_id)) {
      return;
    }
    active_buffers_.emplace_back(buffer_id, bytes);
  }
}

std::optional<DataBufferMetadata>
DataBufferManager::get_metadata(const std::string &buffer_id) {
  SharedMetadata c_meta;
  if (!data_manager_get_metadata(buffer_id.c_str(), &c_meta)) {
    LOG_ERROR("DataBufferManager", buffer_id.c_str(),
              "No metadata found at id\n");
    return std::nullopt;
  }
  // Explicitly construct string_views passing the explicit array boundaries.
  std::string_view buf_id_view(static_cast<const char *>(c_meta.buffer_id),
                               sizeof(c_meta.buffer_id));
  std::string_view inst_name_view(
      static_cast<const char *>(c_meta.instrument_name),
      sizeof(c_meta.instrument_name));
  std::string_view cmd_id_view(static_cast<const char *>(c_meta.command_id),
                               sizeof(c_meta.command_id));
  // Strip away trailing null characters or padding if the C-string is short
  if (auto pos = buf_id_view.find('\0'); pos != std::string_view::npos) {
    buf_id_view = buf_id_view.substr(0, pos);
  }
  if (auto pos = inst_name_view.find('\0'); pos != std::string_view::npos) {
    inst_name_view = inst_name_view.substr(0, pos);
  }
  if (auto pos = cmd_id_view.find('\0'); pos != std::string_view::npos) {
    cmd_id_view = cmd_id_view.substr(0, pos);
  }
  DataBufferMetadata metadata;
  metadata.buffer_id = std::string(buf_id_view);
  metadata.instrument_name = std::string(inst_name_view);
  metadata.command_id = std::string(cmd_id_view);

  metadata.data_type = c_meta.type;
  metadata.element_count = c_meta.element_count;
  metadata.byte_size = c_meta.byte_size;
  metadata.timestamp_ms = c_meta.timestamp_ms;
  metadata.description = "";
  metadata.dimensions = {c_meta.element_count};
  return metadata;
}

void DataBufferManager::release_buffer(const std::string &buffer_id) {
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto loc = std::find_if(
        active_buffers_.begin(), active_buffers_.end(),
        [&buffer_id](const auto &pair) { return pair.first == buffer_id; });
    if (loc != active_buffers_.end()) {
      active_buffers_.erase(loc);
      found = true;
    }
  }

  if (found) {
    data_manager_release_buffer(buffer_id.c_str());
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
  decltype(active_buffers_) buffers_to_release;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // std::move transfers internal pointers; active_buffers_ is left empty
    buffers_to_release = std::move(active_buffers_);
  }
  for (const auto &pair : buffers_to_release) {
    data_manager_release_buffer(pair.first.c_str());
  }
}

bool DataBufferManager::add_offset(const std::string &buffer_id,
                                   double offset) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!contains_buffer_unsafe(buffer_id)) {
      return false;
    }
  }
  return data_manager_add_offset(buffer_id.c_str(), offset);
}

bool DataBufferManager::multiply_gain(const std::string &buffer_id,
                                      double gain) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!contains_buffer_unsafe(buffer_id)) {
      return false;
    }
  }
  return data_manager_multiply_gain(buffer_id.c_str(), gain);
}

} // namespace instserver::ipc
