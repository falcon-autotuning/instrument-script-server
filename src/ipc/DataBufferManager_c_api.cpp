#include "instrument-server/ipc/DataBufferManager_c_api.h"
#include "instrument-server/ipc/DataBufferManager.hpp"
#include <cstring>

extern "C" {

int data_buffer_create(const char *instrument_name, const char *command_id,
                       uint8_t data_type, size_t element_count,
                       const void *data, char *buffer_id_out) {
  if (!instrument_name || !command_id || !buffer_id_out) {
    return -1;
  }

  try {
    auto &manager = instserver::ipc::DataBufferManager::instance();

    auto dtype = static_cast<instserver::ipc::DataType>(data_type);
    std::string buffer_id = manager.create_buffer(instrument_name, command_id,
                                                  dtype, element_count, data);

    if (buffer_id.empty()) {
      return -1;
    }

#ifdef _WIN32
    strncpy_s(buffer_id_out, 128, buffer_id.c_str(), 127);
#else
    strncpy(buffer_id_out, buffer_id.c_str(), 127);
    buffer_id_out[127] = '\0';
#endif

    return 0;
  } catch (...) {
    return -1;
  }
}

size_t data_buffer_total_memory(void) {
  try {
    auto &manager = instserver::ipc::DataBufferManager::instance();
    return manager.total_memory_usage();
  } catch (...) {
    return 0;
  }
}

} // extern "C"
