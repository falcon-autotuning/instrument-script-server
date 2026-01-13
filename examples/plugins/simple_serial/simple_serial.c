#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <instrument-server/plugin/PluginInterface.h>
#include <stdio.h>
#include <string.h>

// No external dependencies - plugin is self-contained

static int g_initialized = 0;

PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {0};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "Simple Serial Plugin", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "SimpleSerial", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description, "Basic serial communication plugin",
          PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

int32_t plugin_initialize(const PluginConfig *config) {
  fprintf(stderr, "[SimpleSerial] Initializing for %s\n",
          config->instrument_name);
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

  if (strcmp(cmd->verb, "ECHO") == 0) {
    resp->success = true;
    snprintf(resp->text_response, PLUGIN_MAX_PAYLOAD, "Echo: %s", cmd->verb);
    return 0;
  }

  resp->success = false;
  strncpy(resp->error_message, "Unknown command", PLUGIN_MAX_STRING_LEN - 1);
  return -1;
}

void plugin_shutdown(void) {
  fprintf(stderr, "[SimpleSerial] Shutting down\n");
  g_initialized = 0;
}
