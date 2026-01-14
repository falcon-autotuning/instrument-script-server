#include <instrument-server/plugin/PluginInterface.h>

#include <stdio.h>
#include <string.h>

static int g_initialized = 0;

PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {0};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "Mock VISA", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "VISA", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description, "Mock VISA plugin for testing",
          PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

int32_t plugin_initialize(const PluginConfig *config) {
  fprintf(stderr, "[MockVISA] Initializing for %s\n", config->instrument_name);
  g_initialized = 1;
  return 0;
}

int32_t plugin_execute_command(const PluginCommand *cmd, PluginResponse *resp) {
  strncpy(resp->command_id, cmd->id, PLUGIN_MAX_STRING_LEN - 1);
  strncpy(resp->instrument_name, cmd->instrument_name,
          PLUGIN_MAX_STRING_LEN - 1);

  if (!g_initialized) {
    resp->success = false;
    strncpy(resp->error_message, "Plugin not initialized",
            PLUGIN_MAX_STRING_LEN - 1);
    return -1;
  }

  // Mock responses for common commands
  if (strcmp(cmd->verb, "*IDN?") == 0 || strcmp(cmd->verb, "IDN") == 0) {
    resp->success = true;
    strncpy(resp->text_response, "Mock Instrument, Model 1234, SN123, v1.0",
            PLUGIN_MAX_PAYLOAD - 1);
    return 0;
  }

  // Default response
  resp->success = true;
  snprintf(resp->text_response, PLUGIN_MAX_PAYLOAD, "Mock VISA OK:  %s",
           cmd->verb);
  return 0;
}

void plugin_shutdown(void) {
  fprintf(stderr, "[MockVISA] Shutting down\n");
  g_initialized = 0;
}
