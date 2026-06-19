#include <instrument-data.h>
#include <instrument-log/inst_logging.h>
#include <instrument-plugin.h>
#include <plugin-api.h>
#include <stdio.h>
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

/* Try to use system strlcpy if available */
#if defined(__has_include)
#if __has_include(<bsd/string.h>)
#include <bsd/string.h>
#define HAVE_STRLCPY 1
#endif
#endif

#if defined(_WIN32)
/* Windows typically does not have strlcpy */
#endif

/* Provide fallback if not available */
#ifndef HAVE_STRLCPY
static size_t strlcpy(char *dst, const char *src, size_t size) {
  size_t src_len = 0;

  if (src) {
    while (src[src_len] != '\0') {
      src_len++;
    }
  }

  if (size != 0) {
    size_t copy_len = (src_len >= size) ? size - 1 : src_len;

    for (size_t i = 0; i < copy_len; i++) {
      dst[i] = src[i];
    }

    dst[copy_len] = '\0';
  }

  return src_len; /* total length of src */
}
#endif

static bool g_initialized = false;
static char instrument_name[PLUGIN_MAX_STRING_LEN] = "";

uint8_t INSTRUMENT_PLUGIN_API plugin_initialize(const PluginConfig *config) {
  if (config == NULL) {
    VISA_LOG_ERROR("plugin_initialize: invalid config\n");
    return 1;
  }

  VISA_LOG_INFO("Initializing for %s\n", config->instrument_name);

  size_t len =
      strlcpy(instrument_name, config->instrument_name, PLUGIN_MAX_STRING_LEN);

  if (len >= PLUGIN_MAX_STRING_LEN) {
    VISA_LOG_ERROR("plugin_initialize: instrument_name truncated (max=%d)\n",
                   PLUGIN_MAX_STRING_LEN);
    return 1;
  }

  g_initialized = true;
  return 0;
}

PluginMetadata INSTRUMENT_PLUGIN_API plugin_get_metadata(void) {
  PluginMetadata meta = {0};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;

  strlcpy(meta.name, "Mock VISA", PLUGIN_MAX_STRING_LEN);
  strlcpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN);
  strlcpy(meta.protocol_type, "VISA", PLUGIN_MAX_STRING_LEN);
  strlcpy(meta.description, "Mock VISA plugin for testing",
          PLUGIN_MAX_STRING_LEN);

  return meta;
}

uint8_t INSTRUMENT_PLUGIN_API plugin_execute_command(const PluginCommand *cmd,
                                                     PluginResponse *resp) {
  if (!g_initialized) {
    VISA_LOG_ERROR("Plugin not initialized");
    return 1;
  }
  VISA_LOG_DEBUG("Started executing");

  // Mock responses for common commands
  if (strcmp(cmd->command, "*IDN?") == 0 || strcmp(cmd->command, "IDN") == 0) {
    Variable var = {0};
    var.type = PARAM_TYPE_STRING;

    strlcpy(var.name, "idn", PLUGIN_MAX_STRING_LEN);
    strlcpy(var.value.str_val, "Mock Instrument, Model 1234, SN123, v1.0",
            PLUGIN_MAX_STRING_LEN);

    plugin_response_push(resp, &var);
    return 0;
  }

  if (strcmp(cmd->command, "ECHO") == 0) {
    Variable var = {0};
    var.type = PARAM_TYPE_STRING;

    strlcpy(var.name, "message", PLUGIN_MAX_STRING_LEN);
    strlcpy(var.value.str_val, "echo", PLUGIN_MAX_STRING_LEN);

    plugin_response_push(resp, &var);
    return 0;
  }

  // ---- GET_DOUBLE ----
  if (strcmp(cmd->command, "GET_DOUBLE") == 0) {
    Variable var = {0};
    var.type = PARAM_TYPE_DOUBLE;
    strlcpy(var.name, "current", PLUGIN_MAX_STRING_LEN);
    var.value.d_val = 3.14;
    plugin_response_push(resp, &var);
    return 0;
  }

  // ---- GET_STRING ----
  if (strcmp(cmd->command, "GET_STRING") == 0) {
    Variable var = {0};
    var.type = PARAM_TYPE_STRING;
    strlcpy(var.name, "message", PLUGIN_MAX_STRING_LEN);
    strlcpy(var.value.str_val, "hello", PLUGIN_MAX_STRING_LEN);
    plugin_response_push(resp, &var);
    return 0;
  }

  // ---- GET_BOOL ----
  if (strcmp(cmd->command, "GET_BOOL") == 0) {
    Variable var = {0};
    var.type = PARAM_TYPE_BOOL;
    strlcpy(var.name, "status", PLUGIN_MAX_STRING_LEN);
    var.value.b_val = true;
    plugin_response_push(resp, &var);
    return 0;
  }

  // ---- GET_ARRAY ----
  if (strcmp(cmd->command, "GET_ARRAY") == 0) {
    // depends on your system design:
    // either PARAM_TYPE_BUFFER or PARAM_TYPE_ARRAY
    double data[1] = {1.0};
    const char *id = data_manager_create_buffer(instrument_name, cmd->id,
                                                INST_DATA_FLOAT64, 1, data);
    Variable var = {0};
    var.type = PARAM_TYPE_BUFFER;
    strlcpy(var.name, "waveform", PLUGIN_MAX_STRING_LEN);
    strlcpy(var.value.str_val, id, PLUGIN_MAX_STRING_LEN);
    plugin_response_push(resp, &var);
    return 0;
  }

  // Default response
  Variable var = {0};
  var.type = PARAM_TYPE_STRING;

  strlcpy(var.name, "idn", PLUGIN_MAX_STRING_LEN);

  int ret = snprintf(var.value.str_val, PLUGIN_MAX_STRING_LEN,
                     "Mock VISA OK: %s\n", cmd->command);

  if (ret < 0) {
    VISA_LOG_ERROR("Default snprintf failed\n");
    return 1;
  }
  if (ret >= PLUGIN_MAX_STRING_LEN) {
    VISA_LOG_ERROR("Default response truncated (max=%d)\n",
                   PLUGIN_MAX_STRING_LEN);
    return 1;
  }

  plugin_response_push(resp, &var);
  return 0;
}

void INSTRUMENT_PLUGIN_API plugin_shutdown(void) {
  VISA_LOG_INFO("Shutting down\n");

  strlcpy(instrument_name, "", PLUGIN_MAX_STRING_LEN);
  g_initialized = false;
}
