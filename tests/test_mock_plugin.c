#include <instrument-server/plugin/PluginInterface.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double g_stored_voltage = 0.0;
static int g_measurement_count = 0;

PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {0};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "Mock VISA", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "MockVISA", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description, "Mock VISA for testing", PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

int32_t plugin_initialize(const PluginConfig *config) {
  fprintf(stderr, "[MockVISA] Initialized for %s\n", config->instrument_name);
  g_stored_voltage = 0.0;
  g_measurement_count = 0;
  return 0;
}

int32_t plugin_execute_command(const PluginCommand *command,
                               PluginResponse *response) {
  strncpy(response->command_id, command->id, PLUGIN_MAX_STRING_LEN - 1);
  strncpy(response->instrument_name, command->instrument_name,
          PLUGIN_MAX_STRING_LEN - 1);

  fprintf(stderr, "[MockVISA] Executing command: %s\n", command->verb);

  if (strcmp(command->verb, "SET_VOLTAGE") == 0) {
    // Extract voltage parameter
    for (uint32_t i = 0; i < command->param_count; i++) {
      if (strcmp(command->params[i].name, "voltage") == 0) {
        if (command->params[i].value.type == PARAM_TYPE_DOUBLE) {
          g_stored_voltage = command->params[i].value.value.d_val;
          fprintf(stderr, "[MockVISA] Set voltage to:  %.3f V\n",
                  g_stored_voltage);
        }
        break;
      }
    }
    response->success = true;
    return 0;
  } else if (strcmp(command->verb, "MEASURE_VOLTAGE") == 0) {
    // Return stored voltage with small noise
    double noise = (rand() % 100 - 50) / 10000.0; // ±0.005V
    double measured = g_stored_voltage + noise;

    g_measurement_count++;
    fprintf(stderr, "[MockVISA] Measured voltage: %.6f V (count: %d)\n",
            measured, g_measurement_count);

    response->success = true;
    response->return_value.type = PARAM_TYPE_DOUBLE;
    response->return_value.value.d_val = measured;

    snprintf(response->text_response, PLUGIN_MAX_PAYLOAD, "%.6f", measured);
    return 0;
  } else if (strcmp(command->verb, "GET_MEASUREMENT_COUNT") == 0) {
    response->success = true;
    response->return_value.type = PARAM_TYPE_INT32;
    response->return_value.value.i32_val = g_measurement_count;
    return 0;
  } else if (strcmp(command->verb, "RESET") == 0) {
    g_stored_voltage = 0.0;
    g_measurement_count = 0;
    response->success = true;
    return 0;
  }

  response->success = false;
  response->error_code = -1;
  strncpy(response->error_message, "Unknown command",
          PLUGIN_MAX_STRING_LEN - 1);
  return -1;
}

void plugin_shutdown(void) {
  fprintf(stderr, "[MockVISA] Shutdown.  Total measurements: %d\n",
          g_measurement_count);
}
