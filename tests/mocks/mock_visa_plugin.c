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
    VISA_LOG_ERROR("plugin_initialize: invalid config");
    return 1;
  }

  VISA_LOG_INFO("Initializing for %s", config->instrument_name);

  size_t len =
      strlcpy(instrument_name, config->instrument_name, PLUGIN_MAX_STRING_LEN);

  if (len >= PLUGIN_MAX_STRING_LEN) {
    VISA_LOG_ERROR("plugin_initialize: instrument_name truncated (max=%d)",
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

  // ---- SET_DOUBLE ----
  if (strcmp(cmd->command, "SET") == 0) {
    uint8_t param_count = param_storage_count(cmd->params);

    for (size_t i = 0; i < param_count; i++) {
      const Variable *var = param_storage_get(cmd->params, i);
      VariableType type = var->type;
      char buf[256];
      snprintf(buf, sizeof(buf), "Value name is %s", var->name);
      VISA_LOG_INFO(buf);
      if (type == PARAM_TYPE_DOUBLE) {
        double value = var->value.d_val;
        VISA_LOG_INFO("Value of type double set to: %f",value);
      } else if (type == PARAM_TYPE_INT64) {
        int64_t value = var->value.i64_val;

        VISA_LOG_INFO("Value of type int64 set to: %d",value);
      }
    }
    return 0;
  }

  // if (verb == "SET") {
  //   double value = 0.0;
  //   for (uint32_t i = 0; i < param_storage_count(command->params); i++) {
  //     const Variable *param = param_storage_get(command->params, i);
  //     if (strcmp(param->name, "arg0") == 0) {
  //       if (param->type == PARAM_TYPE_DOUBLE) {
  //         value = param->value.d_val;
  //         VISA_LOG_INFO("Value of type double set to: %f\n",value);
  //         break;
  //       }
  //       if (param->type == PARAM_TYPE_INT64) {
  //         value = param->value.i64_val;
  //         VISA_LOG_INFO("Value of type int set to: %d\n",value);
  //         break;
  //       }
  //     }
  //   }
  //
  //   if (channel > 0) {
  //     g_channel_values[instrument_name][channel] = value;
  //     VISA_LOG_INFO("Channel: %d\n", channel);
  //   }
  //   VISA_LOG_INFO("OK\n");
  //   return 0;
  // }

  // ---- GET ----
  if (strcmp(cmd->command, "GET") == 0) {
    Variable var = {0};
    var.type = PARAM_TYPE_DOUBLE;
    strlcpy(var.name, "voltage", PLUGIN_MAX_STRING_LEN);
    var.value.d_val = 3.14;
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
  // ---- MEASUERE ----
  if (strcmp(cmd->command, "MEASURE") == 0) {
    Variable var = {0};
    var.type = PARAM_TYPE_DOUBLE;
    strlcpy(var.name, "current", PLUGIN_MAX_STRING_LEN);
    var.value.d_val = 0.01;
    plugin_response_push(resp, &var);
    return 0;
  }

  // ---- CONFIGURE ----
  if (strcmp(cmd->command, "CONFIGURE") == 0) {
    uint8_t param_count = param_storage_count(cmd->params);

    for (size_t i = 0; i < param_count; i++) {
      const Variable *var = param_storage_get(cmd->params, i);
      VariableType type = var->type;
      char buf[256];
      snprintf(buf, sizeof(buf), "Value name is %s", var->name);
      VISA_LOG_INFO(buf);
      if (type == PARAM_TYPE_DOUBLE) {
        double value = var->value.d_val;
        VISA_LOG_INFO("Value of type double set to: %f",value);
      } else if (type == PARAM_TYPE_INT64) {
        int64_t value = var->value.i64_val;

        VISA_LOG_INFO("Value of type int64 set to: %d",value);
      }
    }
    return 0;
  }
  // ---- GET_LARGE_DATA ----
  if (strcmp(cmd->command, "GET_LARGE_DATA") == 0) {
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
                     "Mock VISA OK: %s", cmd->command);

  if (ret < 0) {
    VISA_LOG_ERROR("Default snprintf failed");
    return 1;
  }
  if (ret >= PLUGIN_MAX_STRING_LEN) {
    VISA_LOG_ERROR("Default response truncated (max=%d)",
                   PLUGIN_MAX_STRING_LEN);
    return 1;
  }

  plugin_response_push(resp, &var);
  return 0;
}

void INSTRUMENT_PLUGIN_API plugin_shutdown(void) {
  VISA_LOG_INFO("Shutting down");

  strlcpy(instrument_name, "", PLUGIN_MAX_STRING_LEN);
  g_initialized = false;
}
