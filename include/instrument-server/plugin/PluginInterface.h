#ifndef INSTRUMENT_SCRIPT_PLUGIN_INTERFACE_H
#define INSTRUMENT_SCRIPT_PLUGIN_INTERFACE_H

#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// API version
#define INSTRUMENT_PLUGIN_API_VERSION 1

// Maximum sizes
#define PLUGIN_MAX_STRING_LEN 128
#define PLUGIN_MAX_PARAMS 16
#define PLUGIN_MAX_PAYLOAD 4096

// Parameter value types
typedef enum {
  PARAM_TYPE_NONE = 0,
  PARAM_TYPE_INT32,
  PARAM_TYPE_INT64,
  PARAM_TYPE_UINT32,
  PARAM_TYPE_UINT64,
  PARAM_TYPE_FLOAT,
  PARAM_TYPE_DOUBLE,
  PARAM_TYPE_BOOL,
  PARAM_TYPE_STRING,
  PARAM_TYPE_BINARY,
  PARAM_TYPE_ARRAY_DOUBLE,
  PARAM_TYPE_ARRAY_INT32
} PluginParamType;

// Parameter value union
typedef struct {
  PluginParamType type;
  union {
    int32_t i32_val;
    int64_t i64_val;
    uint32_t u32_val;
    uint64_t u64_val;
    float f_val;
    double d_val;
    bool b_val;
    char str_val[PLUGIN_MAX_STRING_LEN];
    struct {
      uint8_t *data;
      size_t size;
    } binary;
    struct {
      double *data;
      size_t size;
    } array_double;
    struct {
      int32_t *data;
      size_t size;
    } array_int32;
  } value;
} PluginParamValue;

// Command parameter
typedef struct {
  char name[PLUGIN_MAX_STRING_LEN];
  PluginParamValue value;
} PluginParam;

// Command structure
typedef struct {
  char id[PLUGIN_MAX_STRING_LEN];
  char instrument_name[PLUGIN_MAX_STRING_LEN];
  char verb[PLUGIN_MAX_STRING_LEN];
  PluginParam params[PLUGIN_MAX_PARAMS];
  uint32_t param_count;
  uint32_t timeout_ms;
  bool expects_response;
} PluginCommand;

// Response structure
typedef struct {
  char command_id[PLUGIN_MAX_STRING_LEN];
  char instrument_name[PLUGIN_MAX_STRING_LEN];
  bool success;
  PluginParamValue return_value;
  char text_response[PLUGIN_MAX_PAYLOAD];
  uint8_t binary_response[PLUGIN_MAX_PAYLOAD];
  uint32_t binary_response_size;
  int32_t error_code;
  char error_message[PLUGIN_MAX_STRING_LEN];
} PluginResponse;

// Configuration structure (passed during initialization)
typedef struct {
  char instrument_name[PLUGIN_MAX_STRING_LEN];
  char connection_json[PLUGIN_MAX_PAYLOAD]; // Connection config as JSON string
  char api_definition_json[PLUGIN_MAX_PAYLOAD]; // Full API def as JSON string
} PluginConfig;

// Plugin metadata (returned by plugin_get_metadata)
typedef struct {
  uint32_t api_version;
  char name[PLUGIN_MAX_STRING_LEN];
  char version[PLUGIN_MAX_STRING_LEN];
  char protocol_type[PLUGIN_MAX_STRING_LEN];
  char description[PLUGIN_MAX_STRING_LEN];
} PluginMetadata;

// Plugin interface functions (must be implemented by plugin)

/**
 * Get plugin metadata
 * Called before initialization to verify compatibility
 */
PluginMetadata plugin_get_metadata(void);

/**
 * Initialize plugin with configuration
 * Returns 0 on success, non-zero error code on failure
 */
int32_t plugin_initialize(const PluginConfig *config);

/**
 * Execute a command
 * Returns 0 on success, non-zero error code on failure
 * Response is written to the provided response pointer
 */
int32_t plugin_execute_command(const PluginCommand *command,
                               PluginResponse *response);

/**
 * Shutdown and cleanup plugin resources
 */
void plugin_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // INSTRUMENT_SCRIPT_PLUGIN_INTERFACE_H
