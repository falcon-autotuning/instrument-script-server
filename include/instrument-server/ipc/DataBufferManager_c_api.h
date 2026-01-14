#ifndef INSTRUMENT_SERVER_DATA_BUFFER_MANAGER_C_H
#define INSTRUMENT_SERVER_DATA_BUFFER_MANAGER_C_H

#include "instrument-server/export.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * C API for DataBufferManager to be used from plugins
 */

/**
 * Create a data buffer and return its ID
 * @param instrument_name Name of the instrument
 * @param command_id Command that generated this data
 * @param data_type Type of data (0=float32, 1=float64, 2=int32, etc.)
 * @param element_count Number of elements
 * @param data Pointer to data to copy (or NULL to allocate empty buffer)
 * @param buffer_id_out Output buffer for the generated buffer ID (must be at
 * least 128 bytes)
 * @return 0 on success, -1 on failure
 */
INSTRUMENT_SERVER_API int
data_buffer_create(const char *instrument_name, const char *command_id,
                   uint8_t data_type, size_t element_count, const void *data,
                   char *buffer_id_out);

/**
 * Get total memory usage of all buffers
 */
INSTRUMENT_SERVER_API size_t data_buffer_total_memory(void);

#ifdef __cplusplus
}
#endif

#endif // INSTRUMENT_SERVER_DATA_BUFFER_MANAGER_C_H
