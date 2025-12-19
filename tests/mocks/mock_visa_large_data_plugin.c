#include <instrument-server/ipc/DataBufferManager_c_api.h>
#include <instrument-server/plugin/PluginInterface.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_initialized = 0;

PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {0};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "Mock VISA Large Data", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "VISA", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description, "Mock VISA plugin for testing large data buffers",
          PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

int32_t plugin_initialize(const PluginConfig *config) {
  fprintf(stderr, "[MockVISALargeData] Initializing for %s\n",
          config->instrument_name);
  g_initialized = 1;
  return 0;
}

int32_t plugin_execute_command(const PluginCommand *cmd, PluginResponse *resp) {
  strncpy(resp->command_id, cmd->id, PLUGIN_MAX_STRING_LEN - 1);
  strncpy(resp->instrument_name, cmd->instrument_name,
          PLUGIN_MAX_STRING_LEN - 1);

  resp->has_large_data = false;
  resp->data_buffer_id[0] = '\0';
  resp->data_element_count = 0;
  resp->data_type = 0;

  if (!g_initialized) {
    resp->success = false;
    strncpy(resp->error_message, "Plugin not initialized",
            PLUGIN_MAX_STRING_LEN - 1);
    return -1;
  }

  // Small data response
  if (strcmp(cmd->verb, "GET_SMALL_DATA") == 0) {
    resp->success = true;
    strncpy(resp->text_response, "Small data:  42. 0", PLUGIN_MAX_PAYLOAD - 1);
    resp->return_value.type = PARAM_TYPE_DOUBLE;
    resp->return_value.value.d_val = 42.0;
    return 0;
  }

  // Large data response
  if (strcmp(cmd->verb, "GET_LARGE_DATA") == 0) {
    // Generate a large sine wave dataset
    const size_t num_points = 10000;
    float *waveform = (float *)malloc(num_points * sizeof(float));

    if (!waveform) {
      resp->success = false;
      strncpy(resp->error_message, "Failed to allocate waveform data",
              PLUGIN_MAX_STRING_LEN - 1);
      return -1;
    }

    // Generate sine wave
    for (size_t i = 0; i < num_points; i++) {
      waveform[i] = (float)sin(2.0 * M_PI * i / 100.0);
    }

    // Create buffer
    char buffer_id[PLUGIN_MAX_STRING_LEN];
    int result = data_buffer_create(cmd->instrument_name, cmd->id,
                                    0, // FLOAT32
                                    num_points, waveform, buffer_id);

    free(waveform);

    if (result != 0) {
      resp->success = false;
      strncpy(resp->error_message, "Failed to create data buffer",
              PLUGIN_MAX_STRING_LEN - 1);
      return -1;
    }

    // Set response with buffer info
    resp->success = true;
    resp->has_large_data = true;
    strncpy(resp->data_buffer_id, buffer_id, PLUGIN_MAX_STRING_LEN - 1);
    resp->data_element_count = num_points;
    resp->data_type = 0; // FLOAT32

    snprintf(resp->text_response, PLUGIN_MAX_PAYLOAD,
             "Large waveform data: %zu points in buffer %s", num_points,
             buffer_id);

    fprintf(stderr, "[MockVISALargeData] Created buffer %s with %zu points\n",
            buffer_id, num_points);

    return 0;
  }

  // Default response
  resp->success = true;
  snprintf(resp->text_response, PLUGIN_MAX_PAYLOAD, "Mock response:  %s",
           cmd->verb);
  return 0;
}

void plugin_shutdown(void) {
  fprintf(stderr, "[MockVISALargeData] Shutting down\n");
  g_initialized = 0;
}
