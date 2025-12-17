# Instrument Plugin Development Guide

<!--toc:start-->
- [Instrument Plugin Development Guide](#instrument-plugin-development-guide)
  - [Quick Start](#quick-start)
  - [Plugin API Reference](#plugin-api-reference)
    - [Required Functions](#required-functions)
    - [Configuration Parsing](#configuration-parsing)
  - [Parameter Handling](#parameter-handling)
    - [Reading Parameters](#reading-parameters)
    - [Returning Values](#returning-values)
  - [Example: NI-DAQmx Plugin](#example-ni-daqmx-plugin)
  - [CMakeLists.txt for DAQmx Plugin](#cmakeliststxt-for-daqmx-plugin)
  - [Best Practices](#best-practices)
  - [Testing](#testing)
    - [Unit Test Your Plugin](#unit-test-your-plugin)
    - [Integration Test](#integration-test)
  - [Debugging](#debugging)
    - [Enable Debug Logging](#enable-debug-logging)
    - [Attach Debugger](#attach-debugger)
    - [Check IPC Queues](#check-ipc-queues)
  - [Common Issues](#common-issues)
  - [API Versioning](#api-versioning)
<!--toc:end-->

## Quick Start

1. **Copy the example plugin**

   ```bash
   cp -r examples/plugins/simple_serial my_instrument_plugin
   cd my_instrument_plugin
   ```

1. **Edit my_plugin.c**

   - Implement plugin_initialize() for connection setup
   - Implement plugin_execute_command() for command handling
   - Implement plugin_shutdown() for cleanup

1. **Build**

   ```bash
   mkdir build && cd build
   cmake ..  -DCMAKE_PREFIX_PATH=/usr/local
   cmake --build .
   ```

1. **Install**

   ```bash
   sudo cmake --install .
   # Or manually
   sudo cp my_plugin.so /usr/local/lib/instrument-plugins/
   ```

1. **Test**

    ```bash
    instrument-setup test --config my_config.yaml --command TEST_COMMAND
    ```

## Plugin API Reference

### Required Functions

==plugin_get_metadata()==

Returns plugin metadata. Called before initialization.

```C
PluginMetadata plugin_get_metadata(void) {
    PluginMetadata meta = {0};
    meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
    strncpy(meta.name, "My Instrument", PLUGIN_MAX_STRING_LEN - 1);
    strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
    strncpy(meta.protocol_type, "MyProtocol", PLUGIN_MAX_STRING_LEN - 1);
    strncpy(meta.description, "My custom instrument plugin", PLUGIN_MAX_STRING_LEN - 1);
    return meta;
}
```

==plugin_initialize()==

Initialize plugin with configuration. Called once after loading.

```C

int32_t plugin_initialize(const PluginConfig* config) {
    // Parse config->connection_json to get connection parameters
    // Example: {"address": "192.168.1.100", "port": 5025}

    // Open connection to instrument
    // Store connection handle in static/global variable
    
    // Return 0 on success, non-zero error code on failure
    return 0;
}
```

### Configuration Parsing

The ==connection_json== field contains the ==connection== section from your instrument config as a JSON string. Parse it to extract connection parameters:

```C

// Simple manual parsing (use nlohmann/json or similar in C++)
const char*addr_key = "\"address\": \"";
const char* addr_start = strstr(config->connection_json, addr_key);
if (! addr_start) return -1;
addr_start += strlen(addr_key);
const char* addr_end = strchr(addr_start, '"');
size_t len = addr_end - addr_start;
char address[256];
strncpy(address, addr_start, len);
address[len] = '\0';

// Now use 'address' to connect
```

==plugin_execute_command()==

Execute a single instrument command. Called for each context.call() from Lua.

```C

int32_t plugin_execute_command(const PluginCommand*command, PluginResponse* response) {
    // Always fill response metadata
    strncpy(response->command_id, command->id, PLUGIN_MAX_STRING_LEN - 1);
    strncpy(response->instrument_name, command->instrument_name, PLUGIN_MAX_STRING_LEN - 1);

    // Handle commands by verb
    if (strcmp(command->verb, "SET_VOLTAGE") == 0) {
        // Extract parameters
        double voltage = 0.0;
        for (uint32_t i = 0; i < command->param_count; i++) {
            if (strcmp(command->params[i].name, "voltage") == 0) {
                voltage = command->params[i].value.value. d_val;
                break;
            }
        }
        
        // Send command to instrument
        // ...  your protocol-specific code here ...
        
        response->success = true;
        return 0;
    }
    else if (strcmp(command->verb, "MEASURE_VOLTAGE") == 0) {
        // Read from instrument
        double measured_value = 0.0;
        // ... instrument-specific read code ...
        
        // Fill response
        response->success = true;
        response->return_value. type = PARAM_TYPE_DOUBLE;
        response->return_value.value. d_val = measured_value;
        
        char text[64];
        snprintf(text, sizeof(text), "%.6f", measured_value);
        strncpy(response->text_response, text, PLUGIN_MAX_PAYLOAD - 1);
        
        return 0;
    }
    
    // Unknown command
    response->success = false;
    response->error_code = -1;
    strncpy(response->error_message, "Unknown command", PLUGIN_MAX_STRING_LEN - 1);
    return -1;
}
```

==plugin_shutdown()==

Cleanup and close connections. Called before worker exits.

```C

void plugin_shutdown(void) {
    // Close connection handle
    // Free any allocated resources
}
```

## Parameter Handling

### Reading Parameters

Parameters are passed as an array. Iterate to find the one you need:

```C

// Helper function
static double get_double_param(const PluginCommand*cmd, const char* name, double default_val) {
    for (uint32_t i = 0; i < cmd->param_count; i++) {
        if (strcmp(cmd->params[i].name, name) == 0) {
            if (cmd->params[i].value.type == PARAM_TYPE_DOUBLE) {
                return cmd->params[i].value.value.d_val;
            }
        }
    }
    return default_val;
}

// Usage:
double voltage = get_double_param(command, "voltage", 0.0);
```

### Returning Values

Set response->return_value based on what you're returning:

```C

// Returning a double
response->return_value.type = PARAM_TYPE_DOUBLE;
response->return_value. value.d_val = 3.14159;

// Returning an integer
response->return_value. type = PARAM_TYPE_INT64;
response->return_value. value.i64_val = 42;

// Returning a string
response->return_value.type = PARAM_TYPE_STRING;
strncpy(response->return_value.value.str_val, "OK", PLUGIN_MAX_STRING_LEN - 1);

// Returning an array (requires dynamic allocation - simplified here)
response->return_value. type = PARAM_TYPE_ARRAY_DOUBLE;
// Note: For large arrays, consider using binary_response field
```

## Example: NI-DAQmx Plugin

Here's a complete example for NI-DAQmx:

```C

# include <instrument-server/plugin/PluginInterface.h>

# include <NIDAQmx.h>

# include <string. h>

# include <stdio. h>

static TaskHandle g_task = 0;

PluginMetadata plugin_get_metadata(void) {
    PluginMetadata meta = {0};
    meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
    strncpy(meta.name, "NI-DAQmx", PLUGIN_MAX_STRING_LEN - 1);
    strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
    strncpy(meta.protocol_type, "DAQmx", PLUGIN_MAX_STRING_LEN - 1);
    strncpy(meta.description, "National Instruments DAQmx plugin", PLUGIN_MAX_STRING_LEN - 1);
    return meta;
}

int32_t plugin_initialize(const PluginConfig* config) {
    // DAQmx doesn't need upfront initialization
    return 0;
}

int32_t plugin_execute_command(const PluginCommand*command, PluginResponse* response) {
    strncpy(response->command_id, command->id, PLUGIN_MAX_STRING_LEN - 1);
    strncpy(response->instrument_name, command->instrument_name, PLUGIN_MAX_STRING_LEN - 1);

    if (strcmp(command->verb, "DAQmxCreateTask") == 0) {
        TaskHandle task = 0;
        int32 error = DAQmxCreateTask("", &task);
        
        if (error < 0) {
            response->success = false;
            response->error_code = error;
            DAQmxGetExtendedErrorInfo(response->error_message, PLUGIN_MAX_STRING_LEN);
            return error;
        }
        
        g_task = task;
        response->success = true;
        response->return_value.type = PARAM_TYPE_UINT64;
        response->return_value.value.u64_val = (uint64_t)task;
        return 0;
    }
    else if (strcmp(command->verb, "DAQmxCreateAIVoltageChan") == 0) {
        // Extract parameters
        char physical_channel[256] = {0};
        double min_val = -10.0;
        double max_val = 10.0;
        
        for (uint32_t i = 0; i < command->param_count; i++) {
            const PluginParam* p = &command->params[i];
            if (strcmp(p->name, "physicalChannel") == 0 && p->value.type == PARAM_TYPE_STRING) {
                strncpy(physical_channel, p->value.value.str_val, sizeof(physical_channel) - 1);
            }
            else if (strcmp(p->name, "minVal") == 0 && p->value.type == PARAM_TYPE_DOUBLE) {
                min_val = p->value.value. d_val;
            }
            else if (strcmp(p->name, "maxVal") == 0 && p->value.type == PARAM_TYPE_DOUBLE) {
                max_val = p->value.value.d_val;
            }
        }
        
        int32 error = DAQmxCreateAIVoltageChan(
            g_task,
            physical_channel,
            "",
            DAQmx_Val_Cfg_Default,
            min_val,
            max_val,
            DAQmx_Val_Volts,
            NULL
        );
        
        if (error < 0) {
            response->success = false;
            response->error_code = error;
            DAQmxGetExtendedErrorInfo(response->error_message, PLUGIN_MAX_STRING_LEN);
            return error;
        }
        
        response->success = true;
        return 0;
    }
    else if (strcmp(command->verb, "DAQmxReadAnalogF64") == 0) {
        int32 num_samples = 1000;
        double timeout = 10.0;
        
        for (uint32_t i = 0; i < command->param_count; i++) {
            const PluginParam* p = &command->params[i];
            if (strcmp(p->name, "numSampsPerChan") == 0) {
                num_samples = p->value.value.i32_val;
            }
            else if (strcmp(p->name, "timeout") == 0) {
                timeout = p->value.value.d_val;
            }
        }
        
        double* data = malloc(num_samples * sizeof(double));
        if (! data) {
            response->success = false;
            strncpy(response->error_message, "Memory allocation failed", PLUGIN_MAX_STRING_LEN - 1);
            return -1;
        }
        
        int32 samples_read = 0;
        int32 error = DAQmxReadAnalogF64(
            g_task,
            num_samples,
            timeout,
            DAQmx_Val_GroupByChannel,
            data,
            num_samples,
            &samples_read,
            NULL
        );
        
        if (error < 0) {
            free(data);
            response->success = false;
            response->error_code = error;
            DAQmxGetExtendedErrorInfo(response->error_message, PLUGIN_MAX_STRING_LEN);
            return error;
        }
        
        // Pack data into response (simplified - in production use binary_response)
        response->success = true;
        response->return_value.type = PARAM_TYPE_ARRAY_DOUBLE;
        // Note: Need to handle large arrays properly - this is simplified
        
        snprintf(response->text_response, PLUGIN_MAX_PAYLOAD, "Read %d samples", samples_read);
        
        free(data);
        return 0;
    }
    else if (strcmp(command->verb, "DAQmxClearTask") == 0) {
        if (g_task != 0) {
            DAQmxClearTask(g_task);
            g_task = 0;
        }
        response->success = true;
        return 0;
    }
    
    response->success = false;
    strncpy(response->error_message, "Unknown command", PLUGIN_MAX_STRING_LEN - 1);
    return -1;
}

void plugin_shutdown(void) {
    if (g_task != 0) {
        DAQmxStopTask(g_task);
        DAQmxClearTask(g_task);
        g_task = 0;
    }
}
```

## CMakeLists.txt for DAQmx Plugin

```CMake
cmake_minimum_required(VERSION 3.20)
project(DAQmxPlugin VERSION 1.0.0)

find_package(InstrumentServer REQUIRED)

# Find NI-DAQmx

if(WIN32)
    set(NIDAQMX_INCLUDE_DIR "C:/Program Files (x86)/National Instruments/Shared/ExternalCompilerSupport/C/include")
    set(NIDAQMX_LIBRARY "C:/Program Files (x86)/National Instruments/Shared/ExternalCompilerSupport/C/lib64/msvc/NIDAQmx.lib")
else()
    set(NIDAQMX_INCLUDE_DIR "/usr/local/natinst/nidaqmx/include")
    set(NIDAQMX_LIBRARY "/usr/local/natinst/nidaqmx/lib/libnidaqmx.so")
endif()

add_instrument_plugin(daqmx_plugin
    SOURCES daqmx_plugin.c
    INCLUDE_DIRS ${NIDAQMX_INCLUDE_DIR}
    LINK_LIBRARIES ${NIDAQMX_LIBRARY}
)

install(TARGETS daqmx_plugin
    LIBRARY DESTINATION lib/instrument-plugins
)
```

## Best Practices

1. Error Handling

Always set ==response->success== and provide meaningful error messages:

  ```C
  if (error_condition) {
      response->success = false;
      response->error_code = error_code;
      snprintf(response->error_message, PLUGIN_MAX_STRING_LEN,
              "Failed to %s:  %s", operation, error_details);
      return error_code;
  }
  ```

1. Thread Safety

Worker processes are single-threaded by design, but if your plugin uses threads internally:

- Use mutexes to protect shared state
- Don't block indefinitely (respect command timeout)
- Clean up threads in plugin_shutdown()

1. Resource Management

- Open connections in plugin_initialize()
- Store handles in static/global variables
- Always clean up in plugin_shutdown()
- Don't leak memory in plugin_execute_command()

1. Timeouts

Respect the command->timeout_ms field:

```C

// Set socket timeout
struct timeval tv;
tv.tv_sec = command->timeout_ms / 1000;
tv.tv_usec = (command->timeout_ms % 1000) * 1000;
setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
```

1. Large Data

For waveforms or large arrays, use response->binary_response:

```C

// Serialize data to bytes
size_t data_size = num_samples * sizeof(double);
if (data_size <= PLUGIN_MAX_PAYLOAD) {
    memcpy(response->binary_response, data, data_size);
    response->binary_response_size = data_size;
    response->return_value.type = PARAM_TYPE_ARRAY_DOUBLE;
}
```

1. Logging

Use stderr for plugin-level logs (captured by worker):

```C
fprintf(stderr, "[%s] Connecting to %s:%d\n",
        command->instrument_name, address, port);
```

## Testing

### Unit Test Your Plugin

```C
// test_my_plugin.c
# include "my_plugin.c"  // Include implementation
# include <assert.h>

int main() {
    PluginConfig config = {0};
    strncpy(config.instrument_name, "TEST", PLUGIN_MAX_STRING_LEN - 1);
    snprintf(config.connection_json, PLUGIN_MAX_PAYLOAD,
             "{\"device\": \"/dev/null\"}");

    int32_t result = plugin_initialize(&config);
    assert(result == 0);
    
    PluginCommand cmd = {0};
    strncpy(cmd.verb, "TEST_CMD", PLUGIN_MAX_STRING_LEN - 1);
    
    PluginResponse resp = {0};
    result = plugin_execute_command(&cmd, &resp);
    assert(result == 0);
    assert(resp.success == true);
    
    plugin_shutdown();
    
    printf("Plugin tests passed!\n");
    return 0;
}
```

### Integration Test

```bash

# Build plugin
cmake --build build/

# Test with instrument-setup
instrument-setup test \
  --config my_config.yaml \
  --command TEST_COMMAND
```

## Debugging

### Enable Debug Logging

Set environment variable before running worker:

```bash

export INSTRUMENT_WORKER_DEBUG=1
instrument-worker --instrument TEST --plugin ./my_plugin.so
```

### Attach Debugger

Find worker PID and attach:

```bash
ps aux | grep instrument-worker
gdb -p <PID>
```

### Check IPC Queues

```bash
# Linux:  check POSIX message queues
ls -la /dev/mqueue/
# Should see:  instrument_<name>*req and instrument*<name>_resp
```

## Common Issues

1. Plugin Won't Load

Symptom: "Failed to load plugin" error

Solutions:

- Check file permissions: ==chmod +x plugin.so==
- Verify shared library dependencies: ==ldd plugin.so==
- Ensure exported symbols: ==nm -D plugin.so | grep plugin_==

1. Symbol Not Found

Symptom: "undefined symbol: plugin_initialize"

Solutions:

- Add ==extern "C"== if using C++
- Check CMake exports visibility flags
- Verify with: ==nm -D plugin.so==

1. IPC Timeout

Symptom: Commands timeout, no response

Solutions:

- Check worker process is alive: ==ps aux | grep instrument-worker==
- Check worker logs: ==`tail -f worker_<name>.log`==
- Verify queue exists: ==ls /dev/mqueue/==

1. Memory Issues

Symptom: Worker crashes or OOM

Solutions:

- Use valgrind: ==valgrind instrument-worker ... ==
- Check for memory leaks in plugin
- Limit data sizes in responses

## API Versioning

Current API version: ==1==

If API changes incompatibly, version will increment. Plugins must check:

```C

PluginMetadata plugin_get_metadata(void) {
    PluginMetadata meta = {0};
    meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;  // Must match server
    // ...
    return meta;
}
```

Server will refuse to load plugins with mismatched API versions.
