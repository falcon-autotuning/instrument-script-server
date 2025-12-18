#include "instrument-server/plugin/PluginInterface.h"
#include <cstring>
#include <map>
#include <string>
#include <thread>

// Simple mock plugin for testing

static std::map<std::string, std::string> g_responses;
static bool g_initialized = false;

extern "C" {

PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "Mock Test Plugin", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "MockTest", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description, "Mock plugin for integration testing",
          PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

int32_t plugin_initialize(const PluginConfig *config) {
  g_initialized = true;

  // Setup default responses
  g_responses["IDN"] = "Mock Instrument, Model 1234, SN001, v1.0";
  g_responses["RESET"] = "OK";
  g_responses["MEASURE"] = "3.14159";
  g_responses["SET_VOLTAGE"] = "OK";

  return 0;
}

int32_t plugin_execute_command(const PluginCommand *command,
                               PluginResponse *response) {
  strncpy(response->command_id, command->id, PLUGIN_MAX_STRING_LEN - 1);
  strncpy(response->instrument_name, command->instrument_name,
          PLUGIN_MAX_STRING_LEN - 1);

  std::string verb = command->verb;

  // Simulate small processing delay
  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  // Check if we have a response for this verb
  if (g_responses.count(verb)) {
    response->success = true;
    strncpy(response->text_response, g_responses[verb].c_str(),
            PLUGIN_MAX_PAYLOAD - 1);

    // Try to parse as double
    try {
      double val = std::stod(g_responses[verb]);
      response->return_value.type = PARAM_TYPE_DOUBLE;
      response->return_value.value.d_val = val;
    } catch (...) {
      // Not a number
    }

    return 0;
  }

  // Unknown command
  response->success = false;
  strncpy(response->error_message, "Unknown command",
          PLUGIN_MAX_STRING_LEN - 1);
  return -1;
}

void plugin_shutdown(void) { g_initialized = false; }

} // extern "C"
