#include "instrument_script/plugin/PluginInterface.h"
#include <NIDAQmx.h>
#include <cstring>
#include <string>
#include <visa.h>

static ViSession g_default_rm = 0;
static ViSession g_instrument = 0;
static std::string g_resource_address;

extern "C" {

PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "VISA Built-in", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "VISA", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description, "Built-in VISA/SCPI adapter",
          PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

int32_t plugin_initialize(const PluginConfig *config) {
  // Parse connection JSON to get VISA resource address
  // Simplified:  assume connection_json contains {"address": "TCPIP:: ... "}
  // In production, use a JSON parser

  const char *addr_key = "\"address\": \"";
  const char *addr_start = strstr(config->connection_json, addr_key);
  if (!addr_start) {
    return -1;
  }
  addr_start += strlen(addr_key);
  const char *addr_end = strchr(addr_start, '"');
  if (!addr_end) {
    return -1;
  }

  g_resource_address = std::string(addr_start, addr_end - addr_start);

  // Open VISA session
  ViStatus status = viOpenDefaultRM(&g_default_rm);
  if (status < VI_SUCCESS) {
    return status;
  }

  status = viOpen(g_default_rm, const_cast<ViRsrc>(g_resource_address.c_str()),
                  VI_NULL, VI_NULL, &g_instrument);
  if (status < VI_SUCCESS) {
    viClose(g_default_rm);
    return status;
  }

  return 0;
}

int32_t plugin_execute_command(const PluginCommand *command,
                               PluginResponse *response) {
  strncpy(response->command_id, command->id, PLUGIN_MAX_STRING_LEN - 1);
  strncpy(response->instrument_name, command->instrument_name,
          PLUGIN_MAX_STRING_LEN - 1);

  // Simple SCPI command execution
  // In production, do template expansion from verb + params
  std::string scpi_cmd = command->verb; // Simplified
  scpi_cmd += "\n";

  ViUInt32 written;
  ViStatus status =
      viWrite(g_instrument, (ViBuf)scpi_cmd.c_str(), scpi_cmd.size(), &written);

  if (status < VI_SUCCESS) {
    response->success = false;
    response->error_code = status;
    strncpy(response->error_message, "VISA write failed",
            PLUGIN_MAX_STRING_LEN - 1);
    return status;
  }

  // If expects response, read
  if (command->expects_response) {
    char buffer[1024];
    ViUInt32 bytes_read;

    status =
        viRead(g_instrument, (ViBuf)buffer, sizeof(buffer) - 1, &bytes_read);

    if (status < VI_SUCCESS) {
      response->success = false;
      response->error_code = status;
      strncpy(response->error_message, "VISA read failed",
              PLUGIN_MAX_STRING_LEN - 1);
      return status;
    }

    buffer[bytes_read] = '\0';
    strncpy(response->text_response, buffer, PLUGIN_MAX_PAYLOAD - 1);

    // Try to parse as double
    char *endptr;
    double val = strtod(buffer, &endptr);
    if (endptr != buffer) {
      response->return_value.type = PARAM_TYPE_DOUBLE;
      response->return_value.value.d_val = val;
    }
  }

  response->success = true;
  return 0;
}

void plugin_shutdown(void) {
  if (g_instrument) {
    viClose(g_instrument);
    g_instrument = 0;
  }
  if (g_default_rm) {
    viClose(g_default_rm);
    g_default_rm = 0;
  }
}

} // extern "C"
