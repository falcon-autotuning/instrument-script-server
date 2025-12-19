#include <cjson/cJSON.h>
#include <instrument-server/ipc/DataBufferManager_c_api.h>
#include <instrument-server/plugin/PluginInterface.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// NI-VISA includes
#ifdef _WIN32
#include <visa. h>
#else
#include <visa.h>
#endif

// Threshold for using buffer storage (in bytes)
#define LARGE_DATA_THRESHOLD 1024

// Plugin state
typedef struct {
  ViSession default_rm;
  ViSession instrument;
  char resource_address[PLUGIN_MAX_STRING_LEN];
  uint32_t timeout_ms;
  char termination_char[4];
  bool initialized;
} VISAPluginState;

static VISAPluginState g_state = {0};

// Helper:  Replace {param} placeholders in template string
static void substitute_template(const char *template, const PluginCommand *cmd,
                                char *output, size_t output_size) {
  const char *src = template;
  char *dst = output;
  size_t remaining = output_size - 1;

  while (*src && remaining > 0) {
    if (*src == '{') {
      // Find closing brace
      const char *end = strchr(src, '}');
      if (end) {
        // Extract parameter name
        size_t param_len = end - src - 1;
        char param_name[PLUGIN_MAX_STRING_LEN];
        strncpy(param_name, src + 1, param_len);
        param_name[param_len] = '\0';

        // Look up parameter value
        bool found = false;
        for (uint32_t i = 0; i < cmd->param_count; i++) {
          if (strcmp(cmd->params[i].name, param_name) == 0) {
            // Format value based on type
            char value_str[PLUGIN_MAX_STRING_LEN];
            switch (cmd->params[i].value.type) {
            case PARAM_TYPE_INT32:
              snprintf(value_str, sizeof(value_str), "%d",
                       cmd->params[i].value.value.i32_val);
              break;
            case PARAM_TYPE_INT64:
              snprintf(value_str, sizeof(value_str), "%ld",
                       cmd->params[i].value.value.i64_val);
              break;
            case PARAM_TYPE_UINT32:
              snprintf(value_str, sizeof(value_str), "%u",
                       cmd->params[i].value.value.u32_val);
              break;
            case PARAM_TYPE_UINT64:
              snprintf(value_str, sizeof(value_str), "%lu",
                       cmd->params[i].value.value.u64_val);
              break;
            case PARAM_TYPE_FLOAT:
              snprintf(value_str, sizeof(value_str), "%f",
                       cmd->params[i].value.value.f_val);
              break;
            case PARAM_TYPE_DOUBLE:
              snprintf(value_str, sizeof(value_str), "%lf",
                       cmd->params[i].value.value.d_val);
              break;
            case PARAM_TYPE_BOOL:
              snprintf(value_str, sizeof(value_str), "%s",
                       cmd->params[i].value.value.b_val ? "1" : "0");
              break;
            case PARAM_TYPE_STRING:
              strncpy(value_str, cmd->params[i].value.value.str_val,
                      sizeof(value_str) - 1);
              break;
            default:
              value_str[0] = '\0';
            }

            size_t value_len = strlen(value_str);
            if (value_len <= remaining) {
              strcpy(dst, value_str);
              dst += value_len;
              remaining -= value_len;
            }
            found = true;
            break;
          }
        }

        if (!found) {
          // Parameter not found, leave placeholder
          size_t copy_len = (end - src + 1);
          if (copy_len <= remaining) {
            strncpy(dst, src, copy_len);
            dst += copy_len;
            remaining -= copy_len;
          }
        }

        src = end + 1;
        continue;
      }
    }

    *dst++ = *src++;
    remaining--;
  }

  *dst = '\0';
}

// Helper: Parse JSON field safely
static const char *get_json_string(cJSON *json, const char *key,
                                   const char *default_val) {
  cJSON *item = cJSON_GetObjectItem(json, key);
  if (item && cJSON_IsString(item)) {
    return item->valuestring;
  }
  return default_val;
}

static int get_json_int(cJSON *json, const char *key, int default_val) {
  cJSON *item = cJSON_GetObjectItem(json, key);
  if (item && cJSON_IsNumber(item)) {
    return item->valueint;
  }
  return default_val;
}

// Plugin interface implementation

PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {0};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "NI-VISA Plugin", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "VISA", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description,
          "Built-in NI-VISA protocol driver supporting GPIB, USB, Ethernet, "
          "and Serial instruments with large data buffer support",
          PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

int32_t plugin_initialize(const PluginConfig *config) {
  fprintf(stderr, "[VISA] Initializing for instrument: %s\n",
          config->instrument_name);

  // Parse connection configuration
  cJSON *conn_json = cJSON_Parse(config->connection_json);
  if (!conn_json) {
    fprintf(stderr, "[VISA] Failed to parse connection JSON\n");
    return -1;
  }

  // Extract connection parameters
  const char *address = get_json_string(conn_json, "address", "");
  if (strlen(address) == 0) {
    fprintf(stderr, "[VISA] No address specified in connection config\n");
    cJSON_Delete(conn_json);
    return -1;
  }

  strncpy(g_state.resource_address, address, PLUGIN_MAX_STRING_LEN - 1);
  g_state.timeout_ms = get_json_int(conn_json, "timeout", 5000);

  // Get termination character (default to \n)
  const char *term_char = get_json_string(conn_json, "termination", "\\n");
  if (strcmp(term_char, "\\n") == 0) {
    strcpy(g_state.termination_char, "\n");
  } else if (strcmp(term_char, "\\r") == 0) {
    strcpy(g_state.termination_char, "\r");
  } else if (strcmp(term_char, "\\r\\n") == 0) {
    strcpy(g_state.termination_char, "\r\n");
  } else {
    strncpy(g_state.termination_char, term_char,
            sizeof(g_state.termination_char) - 1);
  }

  cJSON_Delete(conn_json);

  // Initialize VISA
  ViStatus status = viOpenDefaultRM(&g_state.default_rm);
  if (status < VI_SUCCESS) {
    fprintf(stderr, "[VISA] Failed to open default resource manager: 0x%08X\n",
            status);
    return -1;
  }

  // Open instrument session
  status = viOpen(g_state.default_rm, g_state.resource_address, VI_NO_LOCK,
                  g_state.timeout_ms, &g_state.instrument);
  if (status < VI_SUCCESS) {
    fprintf(stderr, "[VISA] Failed to open instrument '%s': 0x%08X\n",
            g_state.resource_address, status);
    viClose(g_state.default_rm);
    return -1;
  }

  // Set timeout
  viSetAttribute(g_state.instrument, VI_ATTR_TMO_VALUE, g_state.timeout_ms);

  g_state.initialized = true;

  fprintf(stderr, "[VISA] Successfully connected to '%s'\n",
          g_state.resource_address);

  // Parse API definition to check for initialization commands
  cJSON *api_json = cJSON_Parse(config->api_definition_json);
  if (api_json) {
    cJSON *init_commands = cJSON_GetObjectItem(api_json, "initialization");
    if (init_commands && cJSON_IsArray(init_commands)) {
      fprintf(stderr, "[VISA] Running %d initialization commands\n",
              cJSON_GetArraySize(init_commands));

      // Execute initialization commands
      cJSON *init_cmd = NULL;
      cJSON_ArrayForEach(init_cmd, init_commands) {
        if (cJSON_IsString(init_cmd)) {
          const char *cmd_str = init_cmd->valuestring;
          char full_cmd[PLUGIN_MAX_PAYLOAD];
          snprintf(full_cmd, sizeof(full_cmd), "%s%s", cmd_str,
                   g_state.termination_char);

          ViUInt32 written;
          ViStatus write_status = viWrite(g_state.instrument, (ViBuf)full_cmd,
                                          strlen(full_cmd), &written);

          if (write_status < VI_SUCCESS) {
            fprintf(stderr,
                    "[VISA] Warning: Init command '%s' failed: 0x%08X\n",
                    cmd_str, write_status);
          } else {
            fprintf(stderr, "[VISA] Init command sent: %s\n", cmd_str);
          }
        }
      }
    }
    cJSON_Delete(api_json);
  }

  return 0;
}

int32_t plugin_execute_command(const PluginCommand *cmd, PluginResponse *resp) {
  // Fill in response metadata
  strncpy(resp->command_id, cmd->id, PLUGIN_MAX_STRING_LEN - 1);
  strncpy(resp->instrument_name, cmd->instrument_name,
          PLUGIN_MAX_STRING_LEN - 1);

  // Initialize large data fields
  resp->has_large_data = false;
  resp->data_buffer_id[0] = '\0';
  resp->data_element_count = 0;
  resp->data_type = 0;

  if (!g_state.initialized) {
    resp->success = false;
    strncpy(resp->error_message, "VISA plugin not initialized",
            PLUGIN_MAX_STRING_LEN - 1);
    return -1;
  }

  // The template should be passed in the verb field
  const char *template = cmd->verb;

  // Check if there's a "template" parameter (for explicit override)
  for (uint32_t i = 0; i < cmd->param_count; i++) {
    if (strcmp(cmd->params[i].name, "template") == 0 &&
        cmd->params[i].value.type == PARAM_TYPE_STRING) {
      template = cmd->params[i].value.value.str_val;
      break;
    }
  }

  char command_str[PLUGIN_MAX_PAYLOAD];

  // Substitute template parameters
  substitute_template(template, cmd, command_str, sizeof(command_str));

  // Add termination character
  strncat(command_str, g_state.termination_char,
          sizeof(command_str) - strlen(command_str) - 1);

  fprintf(stderr, "[VISA] Sending:  %s", command_str);

  // Write command to instrument
  ViUInt32 written;
  ViStatus status = viWrite(g_state.instrument, (ViBuf)command_str,
                            strlen(command_str), &written);

  if (status < VI_SUCCESS) {
    resp->success = false;
    snprintf(resp->error_message, PLUGIN_MAX_STRING_LEN,
             "VISA write failed: 0x%08X", status);
    resp->error_code = status;
    return -1;
  }

  // Read response if expected
  if (cmd->expects_response) {
    // Allocate a large buffer for potential large data
    const size_t max_read_size = 1024 * 1024; // 1MB max
    char *read_buffer = (char *)malloc(max_read_size);
    if (!read_buffer) {
      resp->success = false;
      strncpy(resp->error_message, "Failed to allocate read buffer",
              PLUGIN_MAX_STRING_LEN - 1);
      return -1;
    }

    ViUInt32 bytes_read;
    status = viRead(g_state.instrument, (ViBuf)read_buffer, max_read_size - 1,
                    &bytes_read);

    if (status < VI_SUCCESS && status != VI_SUCCESS_MAX_CNT) {
      resp->success = false;
      snprintf(resp->error_message, PLUGIN_MAX_STRING_LEN,
               "VISA read failed:  0x%08X", status);
      resp->error_code = status;
      free(read_buffer);
      return -1;
    }

    read_buffer[bytes_read] = '\0';

    // Trim trailing whitespace/newlines
    while (bytes_read > 0 && (read_buffer[bytes_read - 1] == '\n' ||
                              read_buffer[bytes_read - 1] == '\r' ||
                              read_buffer[bytes_read - 1] == ' ')) {
      read_buffer[--bytes_read] = '\0';
    }

    fprintf(stderr, "[VISA] Received %u bytes\n", bytes_read);

    // Check if data is large enough to use buffer storage
    if (bytes_read > LARGE_DATA_THRESHOLD) {
      fprintf(stderr, "[VISA] Large data detected, using buffer storage\n");

      // Try to parse as array of floats (common case for oscilloscope data)
      // In a real implementation, you'd determine the data type from API
      // definition For now, assume comma or space-separated floats

      // Count delimiters to estimate array size
      size_t delimiter_count = 0;
      for (size_t i = 0; i < bytes_read; i++) {
        if (read_buffer[i] == ',' || read_buffer[i] == ' ' ||
            read_buffer[i] == '\n') {
          delimiter_count++;
        }
      }

      size_t estimated_elements = delimiter_count + 1;
      float *float_array = (float *)malloc(estimated_elements * sizeof(float));

      if (float_array) {
        size_t element_count = 0;
        char *token = strtok(read_buffer, ", \n\r");
        while (token && element_count < estimated_elements) {
          float_array[element_count++] = strtof(token, NULL);
          token = strtok(NULL, ", \n\r");
        }

        // Create buffer
        char buffer_id[PLUGIN_MAX_STRING_LEN];
        int result =
            data_buffer_create(cmd->instrument_name, cmd->id, 0, // 0 = FLOAT32
                               element_count, float_array, buffer_id);

        if (result == 0) {
          // Success - set large data response
          resp->has_large_data = true;
          strncpy(resp->data_buffer_id, buffer_id, PLUGIN_MAX_STRING_LEN - 1);
          resp->data_element_count = element_count;
          resp->data_type = 0; // FLOAT32

          snprintf(resp->text_response, PLUGIN_MAX_PAYLOAD,
                   "Large data buffer:  %s (%zu elements)", buffer_id,
                   element_count);

          fprintf(stderr, "[VISA] Created buffer %s with %zu elements\n",
                  buffer_id, element_count);
        } else {
          fprintf(stderr,
                  "[VISA] Failed to create buffer, falling back to text\n");
          // Fall back to text response (truncated)
          strncpy(resp->text_response, read_buffer, PLUGIN_MAX_PAYLOAD - 1);
        }

        free(float_array);
      }
    } else {
      // Small data - store directly in response
      strncpy(resp->text_response, read_buffer, PLUGIN_MAX_PAYLOAD - 1);

      // Try to parse as numeric value
      char *endptr;
      double dval = strtod(read_buffer, &endptr);
      if (endptr != read_buffer && *endptr == '\0') {
        // Successfully parsed as double
        resp->return_value.type = PARAM_TYPE_DOUBLE;
        resp->return_value.value.d_val = dval;
      } else {
        // Keep as string
        resp->return_value.type = PARAM_TYPE_STRING;
        strncpy(resp->return_value.value.str_val, read_buffer,
                PLUGIN_MAX_STRING_LEN - 1);
      }
    }

    free(read_buffer);
  }

  resp->success = true;
  return 0;
}

void plugin_shutdown(void) {
  fprintf(stderr, "[VISA] Shutting down\n");

  if (g_state.initialized) {
    if (g_state.instrument) {
      viClose(g_state.instrument);
      g_state.instrument = 0;
    }

    if (g_state.default_rm) {
      viClose(g_state.default_rm);
      g_state.default_rm = 0;
    }

    g_state.initialized = false;
  }
}
