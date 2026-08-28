#include <instrument-data.h>
#include <instrument-log/inst_logging.h>
#include <instrument-plugin.h>
#include <math.h>
#include <plugin-api.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VISA_LOG_INFO(fmt, ...)                                                \
  LOG_INFO("Plugin", "MockVISA", fmt, ##__VA_ARGS__)
#define VISA_LOG_DEBUG(fmt, ...)                                               \
  LOG_DEBUG("Plugin", "MockVISA", fmt, ##__VA_ARGS__)
#define VISA_LOG_TRACE(fmt, ...)                                               \
  LOG_TRACE("Plugin", "MockVISA", fmt, ##__VA_ARGS__)
#define VISA_LOG_WARN(fmt, ...)                                                \
  LOG_WARN("Plugin", "MockVISA", fmt, ##__VA_ARGS__)
#define VISA_LOG_ERROR(fmt, ...)                                               \
  LOG_ERROR("Plugin", "MockVISA", fmt, ##__VA_ARGS__)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int g_initialized = 0;
static char instrument_name[PLUGIN_MAX_STRING_LEN] = "";

PluginMetadata INSTRUMENT_PLUGIN_API plugin_get_metadata(void) {
  PluginMetadata meta = {0};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "Mock Large Data", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "LargeMock", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description, "Mock plugin for testing large data buffers",
          PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

uint8_t INSTRUMENT_PLUGIN_API plugin_initialize(const PluginConfig *config) {
  if (config == NULL) {
    VISA_LOG_ERROR("plugin_initialize: config is NULL");
    return 1;
  }
  VISA_LOG_INFO("Initializing for %s", config->instrument_name);
  int ret = snprintf(instrument_name, PLUGIN_MAX_STRING_LEN, "%s",
                     config->instrument_name);
  if (ret < 0) {
    VISA_LOG_ERROR("plugin_initialize: snprintf failed");
    return 1;
  }
  if (ret >= PLUGIN_MAX_STRING_LEN) {
    VISA_LOG_ERROR("plugin_initialize: instrument_name truncated (max=%d)",
                   PLUGIN_MAX_STRING_LEN);
    return 1;
  }
  g_initialized = 1;
  return 0;
}

uint8_t INSTRUMENT_PLUGIN_API plugin_execute_command(const PluginCommand *cmd,
                                                     PluginResponse *resp) {
  if (!g_initialized) {
    VISA_LOG_ERROR("Plugin not initialized\n");
    return 1;
  }

  // Small data response
  if (strcmp(cmd->command, "GET_SMALL_DATA") == 0) {
    Variable var = {0};
    var.type = PARAM_TYPE_DOUBLE;
    strncpy(var.name, "current", PLUGIN_MAX_STRING_LEN - 1);
    var.value.d_val = 42.0;
    VISA_LOG_INFO("Small data: %f", var.value.d_val);
    plugin_response_push(resp, &var);
    return 0;
  }

  // Large data response
  if (strcmp(cmd->command, "GET_LARGE_DATA") == 0) {
    // Generate a large sine wave dataset
    const size_t num_points = 10000;
    float *waveform = (float *)malloc(num_points * sizeof(float));
    if (!waveform) {
      VISA_LOG_ERROR("Failed to allocate waveform data");
      return 2;
    }

    // Generate sine wave
    for (size_t i = 0; i < num_points; i++) {
      waveform[i] = (float)sin(2.0 * M_PI * i / 100.0);
    }

    // Create buffer
    const char *buffer_id = data_manager_create_buffer(
        instrument_name, cmd->id, INST_DATA_FLOAT32, num_points, waveform);
    free(waveform);

    if (buffer_id == NULL) {
      VISA_LOG_ERROR("Failed to create data buffer");
      return 3;
    }

    // Set response with buffer info
    Variable var = {0};
    var.type = PARAM_TYPE_BUFFER;
    strncpy(var.name, "waveform", PLUGIN_MAX_STRING_LEN - 1);
    int err =
        snprintf(var.value.str_val, PLUGIN_MAX_STRING_LEN, "%s", buffer_id);
    if (err < 0 || err >= PLUGIN_MAX_STRING_LEN) {
      VISA_LOG_ERROR(
          "Failed to allocate string for the GET_LARGE_DATA command");
      return 4;
    }
    plugin_response_push(resp, &var);
    VISA_LOG_INFO("Large waveform data: %zu points in buffer %s", num_points,
                  buffer_id);
    return 0;
  }
  // Default response
  Variable var = {0};
  var.type = PARAM_TYPE_STRING;
  strncpy(var.name, "idn", PLUGIN_MAX_STRING_LEN - 1);
  int err = snprintf(var.value.str_val, PLUGIN_MAX_STRING_LEN,
                     "Mock response: %s\n", cmd->command);
  if (err < 0 || err >= PLUGIN_MAX_STRING_LEN) {
    VISA_LOG_ERROR("Failed to allocate string for %s command", cmd->command);
    return 3;
  }
  plugin_response_push(resp, &var);
  return 0;
}

void INSTRUMENT_PLUGIN_API plugin_shutdown(void) {
  VISA_LOG_INFO("Shutting down");
  int err = snprintf(instrument_name, PLUGIN_MAX_STRING_LEN, "");
  if (err < 0 || err >= PLUGIN_MAX_STRING_LEN) {
    VISA_LOG_ERROR("Could not deallocate the instrument_name");
  }
  g_initialized = 0;
}
