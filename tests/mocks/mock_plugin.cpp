#include <instrument-log/inst_logging.h>
#include <instrument-plugin.h>
#include <plugin-api.h>

#include <atomic>
#include <cstring>
#include <map>
#include <string>
#include <thread>

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

// Enhanced mock plugin for comprehensive testing

static std::map<std::string, std::map<int, double>> g_channel_values;
static std::map<std::string, std::string> g_responses;
static std::atomic<int> g_call_count{0};
static bool g_initialized = false;
static char instrument_name[PLUGIN_MAX_STRING_LEN] = "";

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

uint8_t plugin_initialize(const PluginConfig *config) {
  VISA_LOG_INFO("Initializing for %s\n", config->instrument_name);
  int err = snprintf(instrument_name, PLUGIN_MAX_STRING_LEN, "%s",
                     config->instrument_name);
  if (err < 0 || err >= PLUGIN_MAX_STRING_LEN) {
    VISA_LOG_ERROR("instrument_name truncated or error occurred\n");
    return 1;
  }
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

  g_initialized = true;
  return 0;
}

uint8_t plugin_execute_command(const PluginCommand *command,
                               PluginResponse *response) {
  std::string verb = command->command;
  g_call_count++;

  // Simulate small processing delay
  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  // Handle channel-specific commands
  int channel = -1;
  for (uint32_t i = 0; i < param_storage_count(command->params); i++) {
    const Variable *param = param_storage_get(command->params, i);
    if (strcmp(param->name, "channel") == 0) {
      if (param->type == PARAM_TYPE_INT64) {
        channel = param->value.i64_val;
      }
      break;
    }
  }

  // ECHO command
  if (verb == "ECHO") {
    VISA_LOG_INFO("Echo response\n");
    return 0;
  }

  // MEASURE command
  if (verb == "MEASURE") {
    Variable var = {0};
    var.type = PARAM_TYPE_DOUBLE;
    var.value.d_val = 3.14159;
    plugin_response_push(response, &var);
    VISA_LOG_INFO("3.14159\n");
    return 0;
  }

  // SET command
  if (verb == "SET") {
    double value = 0.0;
    for (uint32_t i = 0; i < param_storage_count(command->params); i++) {
      const Variable *param = param_storage_get(command->params, i);
      if (strcmp(param->name, "arg0") == 0) {
        if (param->type == PARAM_TYPE_DOUBLE) {
          value = param->value.d_val;
          if (param->type == PARAM_TYPE_INT64) {
            value = param->value.i64_val;
          }
          break;
        }
      }
    }

    if (channel > 0) {
      g_channel_values[instrument_name][channel] = value;
    }
    VISA_LOG_INFO("OK\n");
    return 0;
  }

  // GET command
  if (verb == "GET") {
    double value = 0.0;
    if (channel > 0 && g_channel_values[instrument_name].count(channel)) {
      value = g_channel_values[instrument_name][channel];
    }
    Variable var = {0};
    var.type = PARAM_TYPE_DOUBLE;
    var.value.d_val = value;
    plugin_response_push(response, &var);
    VISA_LOG_INFO("%.6f\n", value);
    return 0;
  }

  // GET_DOUBLE command
  if (verb == "GET_DOUBLE") {
    Variable var = {0};
    var.type = PARAM_TYPE_DOUBLE;
    var.value.d_val = 2.71828;
    plugin_response_push(response, &var);
    return 0;
  }

  // GET_STRING command
  if (verb == "GET_STRING") {
    VISA_LOG_INFO("test_string\n");
    Variable var = {0};
    var.type = PARAM_TYPE_STRING;
    strncpy(var.name, "idn", PLUGIN_MAX_STRING_LEN - 1);
    int err = snprintf(var.value.str_val, PLUGIN_MAX_STRING_LEN, "test_string");
    if (err != 0) {
      VISA_LOG_ERROR("Failed to allocate string for GET_STRING command\n");
      return 2;
    }
    plugin_response_push(response, &var);
    return 0;
  }

  // GET_BOOL command
  if (verb == "GET_BOOL") {
    Variable var = {0};
    var.type = PARAM_TYPE_BOOL;
    var.value.b_val = true;
    plugin_response_push(response, &var);
    return 0;
  }

  // CONFIGURE command (accepts table parameters)
  if (verb == "CONFIGURE") {
    VISA_LOG_INFO("Configured\n");
    return 0;
  }

  // IDN command
  if (verb == "IDN") {
    VISA_LOG_INFO(g_responses["IDN"].c_str());
    return 0;
  }

  // Unknown command
  VISA_LOG_ERROR("Unknown command: %s\n", verb.c_str());
  return 3;
}

void plugin_shutdown(void) {
  g_initialized = false;
  g_channel_values.clear();
}

} // extern "C"
