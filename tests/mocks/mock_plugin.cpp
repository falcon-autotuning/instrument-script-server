#include "instrument-server/plugin/PluginInterface.h"

#include <atomic>
#include <cstring>
#include <map>
#include <string>
#include <thread>

// Enhanced mock plugin for comprehensive testing

static std::map<std::string, std::map<int, double>> g_channel_values;
static std::map<std::string, std::string> g_responses;
static std::atomic<int> g_call_count{0};
static bool g_initialized = false;

extern "C" {

PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "Enhanced Mock Test Plugin", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "2.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "MockTest", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description, "Enhanced mock plugin for comprehensive testing",
          PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

int32_t plugin_initialize(const PluginConfig *config) {
  g_initialized = true;
  g_call_count = 0;

  // Setup default responses
  g_responses["ECHO"] = "Echo response";
  g_responses["MEASURE"] = "3.14159";
  g_responses["IDN"] = "Mock Instrument, Model 1234, SN001, v2.0";
  g_responses["GET_DOUBLE"] = "2.71828";
  g_responses["GET_STRING"] = "test_string";
  g_responses["GET_BOOL"] = "true";

  // Initialize channel values
  g_channel_values[config->instrument_name][1] = 0.0;
  g_channel_values[config->instrument_name][2] = 0.0;
  g_channel_values[config->instrument_name][3] = 0.0;

  return 0;
}

int32_t plugin_execute_command(const PluginCommand *command,
                               PluginResponse *response) {
  strncpy(response->command_id, command->id, PLUGIN_MAX_STRING_LEN - 1);
  strncpy(response->instrument_name, command->instrument_name,
          PLUGIN_MAX_STRING_LEN - 1);

  std::string verb = command->verb;
  std::string inst_name = command->instrument_name;

  g_call_count++;

  // Simulate small processing delay
  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  // Handle channel-specific commands
  int channel = -1;
  for (uint32_t i = 0; i < command->param_count; i++) {
    if (strcmp(command->params[i].name, "channel") == 0) {
      if (command->params[i].value.type == PARAM_TYPE_INT64) {
        channel = command->params[i].value.value.i64_val;
      }
      break;
    }
  }

  // ECHO command
  if (verb == "ECHO") {
    response->success = true;
    strncpy(response->text_response, "Echo response", PLUGIN_MAX_PAYLOAD - 1);
    return 0;
  }

  // MEASURE command
  if (verb == "MEASURE") {
    response->success = true;
    snprintf(response->text_response, PLUGIN_MAX_PAYLOAD, "3.14159");
    response->return_value.type = PARAM_TYPE_DOUBLE;
    response->return_value.value.d_val = 3.14159;
    return 0;
  }

  // SET command
  if (verb == "SET") {
    double value = 0.0;
    for (uint32_t i = 0; i < command->param_count; i++) {
      if (strcmp(command->params[i].name, "arg0") == 0) {
        if (command->params[i].value.type == PARAM_TYPE_DOUBLE) {
          value = command->params[i].value.value.d_val;
        } else if (command->params[i].value.type == PARAM_TYPE_INT64) {
          value = command->params[i].value.value.i64_val;
        }
        break;
      }
    }

    if (channel > 0) {
      g_channel_values[inst_name][channel] = value;
    }

    response->success = true;
    strncpy(response->text_response, "OK", PLUGIN_MAX_PAYLOAD - 1);
    return 0;
  }

  // GET command
  if (verb == "GET") {
    double value = 0.0;
    if (channel > 0 && g_channel_values[inst_name].count(channel)) {
      value = g_channel_values[inst_name][channel];
    }

    response->success = true;
    snprintf(response->text_response, PLUGIN_MAX_PAYLOAD, "%.6f", value);
    response->return_value.type = PARAM_TYPE_DOUBLE;
    response->return_value.value.d_val = value;
    return 0;
  }

  // GET_DOUBLE command
  if (verb == "GET_DOUBLE") {
    response->success = true;
    response->return_value.type = PARAM_TYPE_DOUBLE;
    response->return_value.value.d_val = 2.71828;
    return 0;
  }

  // GET_STRING command
  if (verb == "GET_STRING") {
    response->success = true;
    strncpy(response->text_response, "test_string", PLUGIN_MAX_PAYLOAD - 1);
    response->return_value.type = PARAM_TYPE_STRING;
    strncpy(response->return_value.value.str_val, "test_string",
            PLUGIN_MAX_STRING_LEN - 1);
    return 0;
  }

  // GET_BOOL command
  if (verb == "GET_BOOL") {
    response->success = true;
    response->return_value.type = PARAM_TYPE_BOOL;
    response->return_value.value.b_val = true;
    return 0;
  }

  // CONFIGURE command (accepts table parameters)
  if (verb == "CONFIGURE") {
    response->success = true;
    strncpy(response->text_response, "Configured", PLUGIN_MAX_PAYLOAD - 1);
    return 0;
  }

  // IDN command
  if (verb == "IDN") {
    response->success = true;
    strncpy(response->text_response, g_responses["IDN"].c_str(),
            PLUGIN_MAX_PAYLOAD - 1);
    return 0;
  }

  // Unknown command
  response->success = false;
  strncpy(response->error_message, "Unknown command",
          PLUGIN_MAX_STRING_LEN - 1);
  return -1;
}

void plugin_shutdown(void) {
  g_initialized = false;
  g_channel_values.clear();
}

} // extern "C"
