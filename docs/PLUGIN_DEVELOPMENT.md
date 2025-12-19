# Plugin Development Guide

Complete guide to developing instrument driver plugins for the Falcon Instrument Server.

## Table of Contents

<!--toc:start-->
- [Plugin Development Guide](#plugin-development-guide)
  - [Table of Contents](#table-of-contents)
  - [Overview](#overview)
    - [Why Plugins?](#why-plugins)
    - [Plugin Types](#plugin-types)
  - [Built-in Plugins](#built-in-plugins)
  - [Plugin Interface](#plugin-interface)
    - [Required Functions](#required-functions)
    - [Header File](#header-file)
  - [Quick Start](#quick-start)
    - [Step 1: Install InstrumentServer](#step-1-install-instrumentserver)
    - [Step 2: Create Your Plugin Project](#step-2-create-your-plugin-project)
    - [Step 3: Build and Install](#step-3-build-and-install)
    - [Step 4: Test Your Plugin](#step-4-test-your-plugin)
  - [Development Workflow](#development-workflow)
    - [Using add_instrument_plugin() Helper](#using-addinstrumentplugin-helper)
    - [Without add_instrument_plugin() Helper](#without-addinstrumentplugin-helper)
  - [Building Plugins](#building-plugins)
    - [Linux](#linux)
    - [macOS](#macos)
    - [Windows](#windows)
    - [Cross-Platform CMake](#cross-platform-cmake)
  - [Testing Plugins](#testing-plugins)
    - [1. Verify Plugin Loads](#1-verify-plugin-loads)
    - [2. Test Metadata](#2-test-metadata)
    - [3. Test Commands](#3-test-commands)
    - [4. Integration Testing](#4-integration-testing)
    - [5. Unit Testing Plugin Directly](#5-unit-testing-plugin-directly)
  - [Example Plugins](#example-plugins)
    - [Example 1: Simple Echo Plugin (No Dependencies)](#example-1-simple-echo-plugin-no-dependencies)
    - [Example 2: Serial Port Plugin](#example-2-serial-port-plugin)
    - [Example 3: VISA Plugin](#example-3-visa-plugin)
  - [Handling Large Data in Plugins](#handling-large-data-in-plugins)
    - [When to Use Data Buffers](#when-to-use-data-buffers)
    - [Creating Data Buffers in C Plugins](#creating-data-buffers-in-c-plugins)
    - [Data Type Codes](#data-type-codes)
    - [Buffer Lifecycle](#buffer-lifecycle)
    - [Linking to DataBufferManager](#linking-to-databuffermanager)
    - [Example: Oscilloscope Plugin](#example-oscilloscope-plugin)
  - [See Also](#see-also)
<!--toc:end-->

## Overview

Plugins are **shared libraries** (. so on Linux, .dll on Windows, .dylib on macOS) that implement the instrument driver interface. The server loads plugins dynamically at runtime.

### Why Plugins?

- **Modularity**: Add new instruments without recompiling server
- **Isolation**: Plugin crashes don't affect server
- **Flexibility**: Use any SDK (VISA, vendor SDKs, custom protocols)
- **Distribution**:  Distribute plugins independently
- **Easy Development**: Use `add_instrument_plugin()` CMake helper

### Plugin Types

| Type | Description | Examples |
|------|-------------|----------|
| **Protocol adapter** | Generic protocol handler | VISA, Serial, TCP |
| **Vendor SDK** | Wraps vendor library | NI-DAQmx, Keithley SDK |
| **Custom protocol** | Proprietary communication | Custom serial, USB HID |

## Built-in Plugins

The server includes the following built-in plugins that are **automatically loaded**:

| Protocol | Description | Status |
|----------|-------------|--------|
| **VISA** | NI-VISA protocol for GPIB, USB, Ethernet, Serial | ✅ Built-in |

These plugins are available without any configuration.  You only need to create custom plugins for:

- Proprietary vendor SDKs (NI-DAQmx, Keithley, etc.)
- Custom protocols not covered by VISA
- Special communication requirements

## Plugin Interface

### Required Functions

Every plugin must export these four C functions:

```c
PluginMetadata plugin_get_metadata(void);
int32_t plugin_initialize(const PluginConfig *config);
int32_t plugin_execute_command(const PluginCommand *cmd, PluginResponse *resp);
void plugin_shutdown(void);
```

### Header File

**Location**: `<instrument-server/plugin/PluginInterface.h>`

After installing InstrumentServer, this header is available at:

- Linux/macOS: `/usr/local/include/instrument-server/plugin/PluginInterface.h`
- Windows: `C:\Program Files\InstrumentServer\include\instrument-server\plugin\PluginInterface.h`

```c
#ifndef INSTRUMENT_PLUGIN_INTERFACE_H
#define INSTRUMENT_PLUGIN_INTERFACE_H

#include <stdint.h>

#define INSTRUMENT_PLUGIN_API_VERSION 1
#define PLUGIN_MAX_STRING_LEN 256
#define PLUGIN_MAX_PAYLOAD 4096
#define PLUGIN_MAX_PARAMS 32

// Parameter types
typedef enum {
    PARAM_TYPE_NONE = 0,
    PARAM_TYPE_DOUBLE = 1,
    PARAM_TYPE_INT64 = 2,
    PARAM_TYPE_STRING = 3,
    PARAM_TYPE_BOOL = 4,
    PARAM_TYPE_UINT64 = 5
} ParamType;

// Parameter value union
typedef struct {
    ParamType type;
    union {
        double d_val;
        int64_t i64_val;
        uint64_t u64_val;
        char str_val[PLUGIN_MAX_STRING_LEN];
        bool b_val;
    } value;
} PluginParamValue;

// Named parameter
typedef struct {
    char name[PLUGIN_MAX_STRING_LEN];
    PluginParamValue value;
} PluginParam;

// Plugin metadata
typedef struct {
    uint32_t api_version;
    char name[PLUGIN_MAX_STRING_LEN];
    char version[PLUGIN_MAX_STRING_LEN];
    char protocol_type[PLUGIN_MAX_STRING_LEN];
    char description[PLUGIN_MAX_STRING_LEN];
} PluginMetadata;

// Plugin configuration
typedef struct {
    char instrument_name[PLUGIN_MAX_STRING_LEN];
    char connection_json[PLUGIN_MAX_PAYLOAD];  // JSON string
} PluginConfig;

// Command from server
typedef struct {
    char id[PLUGIN_MAX_STRING_LEN];
    char instrument_name[PLUGIN_MAX_STRING_LEN];
    char verb[PLUGIN_MAX_STRING_LEN];
    bool expects_response;
    
    uint32_t param_count;
    PluginParam params[PLUGIN_MAX_PARAMS];
} PluginCommand;

// Response to server
typedef struct {
    char command_id[PLUGIN_MAX_STRING_LEN];
    char instrument_name[PLUGIN_MAX_STRING_LEN];
    
    bool success;
    int32_t error_code;
    char error_message[PLUGIN_MAX_STRING_LEN];
    
    char text_response[PLUGIN_MAX_PAYLOAD];
    PluginParamValue return_value;
} PluginResponse;

// Plugin API functions (must be exported)
#ifdef __cplusplus
extern "C" {
#endif

PluginMetadata plugin_get_metadata(void);
int32_t plugin_initialize(const PluginConfig *config);
int32_t plugin_execute_command(const PluginCommand *cmd, PluginResponse *resp);
void plugin_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // INSTRUMENT_PLUGIN_INTERFACE_H
```

## Quick Start

This is the **recommended workflow** for creating a custom plugin after installing InstrumentServer.

### Step 1: Install InstrumentServer

```bash
# Clone and build InstrumentServer
git clone https://github.com/falcon-autotuning/instrument-script-server.git
cd instrument-script-server
mkdir build && cd build
cmake .. 
cmake --build .
sudo cmake --install .
```

This installs:

- Headers to `/usr/local/include/instrument-server/`
- CMake config to `/usr/local/lib/cmake/InstrumentServer/`
- Helper function `add_instrument_plugin()`

### Step 2: Create Your Plugin Project

```bash
mkdir my_instrument_plugin
cd my_instrument_plugin
```

**Create `CMakeLists.txt`:**

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyInstrumentPlugin VERSION 1.0.0)

# Find the installed InstrumentServer package
find_package(InstrumentServer REQUIRED)

# Create your plugin using the helper function
add_instrument_plugin(my_instrument_plugin
  SOURCES 
    my_plugin.c
  LINK_LIBRARIES
    # Add your instrument's SDK here
    # my_instrument_sdk
)

# Install to standard location
install(TARGETS my_instrument_plugin
  LIBRARY DESTINATION lib/instrument-plugins
)
```

**Create `my_plugin.c`:**

```c
#include <instrument-server/plugin/PluginInterface.h>
#include <string.h>

// Your instrument SDK includes
// #include <my_instrument_sdk.h>

static void *g_device_handle = NULL;

PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {0};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "My Instrument Plugin", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta. version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "MyInstrument", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description, "Driver for my custom instrument", PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

int32_t plugin_initialize(const PluginConfig *config) {
  // Parse config->connection_json to get connection parameters
  // Open connection to instrument
  // g_device_handle = my_sdk_open(... );
  
  if (g_device_handle == NULL) {
    return -1;
  }
  
  return 0;
}

int32_t plugin_execute_command(const PluginCommand *cmd, PluginResponse *resp) {
  strncpy(resp->command_id, cmd->id, PLUGIN_MAX_STRING_LEN - 1);
  strncpy(resp->instrument_name, cmd->instrument_name, PLUGIN_MAX_STRING_LEN - 1);

  if (strcmp(cmd->verb, "IDN") == 0) {
    // Query instrument identity
    // char idn[256];
    // my_sdk_query(g_device_handle, "*IDN?", idn);
    
    resp->success = true;
    strncpy(resp->text_response, "My Instrument v1.0", PLUGIN_MAX_PAYLOAD - 1);
    return 0;
  }
  
  if (strcmp(cmd->verb, "MEASURE") == 0) {
    // Perform measurement
    // double value = my_sdk_measure(g_device_handle);
    
    resp->success = true;
    resp->return_value.type = PARAM_TYPE_DOUBLE;
    resp->return_value.value.d_val = 3.14159;  // Replace with actual measurement
    return 0;
  }
  
  resp->success = false;
  strncpy(resp->error_message, "Unknown command", PLUGIN_MAX_STRING_LEN - 1);
  return -1;
}

void plugin_shutdown(void) {
  if (g_device_handle) {
    // my_sdk_close(g_device_handle);
    g_device_handle = NULL;
  }
}
```

### Step 3: Build and Install

```bash
mkdir build && cd build
cmake .. 
cmake --build .
sudo cmake --install .
```

Your plugin is now installed to `/usr/local/lib/instrument-plugins/my_instrument_plugin.so`

### Step 4: Test Your Plugin

Create instrument configuration:

```yaml
# my_instrument. yaml
name: MyInstrument1
api_ref: my_api. yaml
connection: 
  type: MyInstrument  # Must match plugin's protocol_type
  address: "COM3"      # Or whatever your instrument needs
```

```yaml
# my_api.yaml
protocol: 
  type: MyInstrument

commands:
  IDN:
    description: "Identity query"
    template: "IDN"
    response_type: string
    
  MEASURE:
    description:  "Perform measurement"
    template: "MEASURE"
    response_type: double
```

Test the plugin:

```bash
# Test with explicit plugin path
instrument-server test my_instrument.yaml IDN --plugin ./build/my_instrument_plugin.so

# After installation, test without path
instrument-server test my_instrument.yaml IDN

# If successful, start using it
instrument-server daemon start
instrument-server start my_instrument.yaml
instrument-server status MyInstrument1
```

## Development Workflow

### Using add_instrument_plugin() Helper

The `add_instrument_plugin()` CMake function (provided by InstrumentServer) automatically handles:

✅ Creating MODULE library (for dynamic loading)  
✅ Removing `lib` prefix  
✅ Setting correct suffix (. so/. dll/. dylib)  
✅ Position-independent code  
✅ Symbol visibility  
✅ Include paths  

**Basic usage:**

```cmake
find_package(InstrumentServer REQUIRED)

add_instrument_plugin(my_plugin
  SOURCES my_plugin. c
)
```

**With SDK dependencies:**

```cmake
add_instrument_plugin(my_plugin
  SOURCES 
    my_plugin.c
    helper. c
  LINK_LIBRARIES
    my_vendor_sdk
  INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
```

**Multiple plugins:**

```cmake
add_instrument_plugin(plugin1 SOURCES plugin1.c)
add_instrument_plugin(plugin2 SOURCES plugin2.c)
add_instrument_plugin(plugin3 SOURCES plugin3.c)
```

### Without add_instrument_plugin() Helper

If you prefer manual control or can't use the helper:

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyPlugin)

add_library(my_plugin MODULE my_plugin.c)

set_target_properties(my_plugin PROPERTIES
  PREFIX ""                          # No 'lib' prefix
  POSITION_INDEPENDENT_CODE ON
  C_VISIBILITY_PRESET default        # Export symbols
)

target_include_directories(my_plugin PRIVATE
  /usr/local/include  # Or wherever InstrumentServer is installed
)

# Link your SDK
target_link_libraries(my_plugin PRIVATE my_instrument_sdk)

install(TARGETS my_plugin LIBRARY DESTINATION lib/instrument-plugins)
```

## Building Plugins

### Linux

**With CMake (recommended):**

```bash
mkdir build && cd build
cmake .. 
cmake --build .
sudo cmake --install .
```

**Manual compilation (for simple plugins):**

```bash
gcc -shared -fPIC -o my_plugin.so my_plugin.c \
    -I/usr/local/include \
    -L/usr/local/lib -lmy_sdk
```

### macOS

**With CMake:**

```bash
mkdir build && cd build
cmake ..
cmake --build .
sudo cmake --install .
```

**Manual compilation:**

```bash
clang -shared -fPIC -o my_plugin.dylib my_plugin. c \
      -I/usr/local/include \
      -L/usr/local/lib -lmy_sdk
```

### Windows

**With CMake and MSVC:**

```powershell
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019"
cmake --build .  --config Release
cmake --install . --config Release
```

**With MinGW:**

```powershell
gcc -shared -o my_plugin. dll my_plugin.c ^
    -I"C:\Program Files\InstrumentServer\include" ^
    -L"C:\Program Files\MySDK\lib" -lmy_sdk
```

### Cross-Platform CMake

```cmake
cmake_minimum_required(VERSION 3.20)
project(MyPlugin)

find_package(InstrumentServer REQUIRED)

# Works on all platforms
add_instrument_plugin(my_plugin
  SOURCES my_plugin.c
  LINK_LIBRARIES my_sdk
)

# Platform-specific options
if(WIN32)
  target_compile_definitions(my_plugin PRIVATE WINDOWS_SPECIFIC)
elseif(APPLE)
  target_compile_definitions(my_plugin PRIVATE MACOS_SPECIFIC)
else()
  target_compile_definitions(my_plugin PRIVATE LINUX_SPECIFIC)
endif()

install(TARGETS my_plugin LIBRARY DESTINATION lib/instrument-plugins)
```

## Testing Plugins

### 1. Verify Plugin Loads

```bash
# Check if plugin is discoverable
instrument-server plugins

# Should list your plugin if installed to standard location
```

### 2. Test Metadata

```bash
# Test with explicit path
instrument-server discover /path/to/my/plugins

# Should show plugin details
```

### 3. Test Commands

```bash
# Create minimal config files (shown in Quick Start)
# Then test individual commands

instrument-server test my_instrument.yaml IDN
instrument-server test my_instrument.yaml MEASURE
```

### 4. Integration Testing

```bash
# Start daemon
instrument-server daemon start

# Start instrument
instrument-server start my_instrument. yaml

# Check status
instrument-server status MyInstrument1

# Run measurement script
instrument-server measure dc test_measurement.lua

# Cleanup
instrument-server stop MyInstrument1
instrument-server daemon stop
```

### 5. Unit Testing Plugin Directly

Create a test program:

```c
// test_plugin.c
#include <assert.h>
#include <stdio.h>
#include <dlfcn.h>
#include <instrument-server/plugin/PluginInterface.h>

int main() {
  // Load plugin
  void *handle = dlopen("./my_plugin.so", RTLD_NOW);
  assert(handle != NULL);
  
  // Get metadata
  typedef PluginMetadata (*GetMetadataFunc)(void);
  GetMetadataFunc get_metadata = dlsym(handle, "plugin_get_metadata");
  assert(get_metadata != NULL);
  
  PluginMetadata meta = get_metadata();
  printf("Plugin:  %s v%s\n", meta.name, meta.version);
  printf("Protocol: %s\n", meta.protocol_type);
  assert(meta.api_version == INSTRUMENT_PLUGIN_API_VERSION);
  
  // Test initialize
  typedef int32_t (*InitFunc)(const PluginConfig*);
  InitFunc init = dlsym(handle, "plugin_initialize");
  assert(init != NULL);
  
  PluginConfig config = {0};
  strncpy(config.instrument_name, "Test", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(config.connection_json, "{\"address\":\"test\"}", PLUGIN_MAX_PAYLOAD - 1);
  
  int result = init(&config);
  printf("Initialize result: %d\n", result);
  assert(result == 0);
  
  // Test command
  typedef int32_t (*ExecFunc)(const PluginCommand*, PluginResponse*);
  ExecFunc exec = dlsym(handle, "plugin_execute_command");
  assert(exec != NULL);
  
  PluginCommand cmd = {0};
  strncpy(cmd.id, "test-1", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(cmd.instrument_name, "Test", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(cmd.verb, "IDN", PLUGIN_MAX_STRING_LEN - 1);
  cmd.expects_response = true;
  
  PluginResponse resp = {0};
  result = exec(&cmd, &resp);
  printf("Command result: %d, success: %d\n", result, resp.success);
  printf("Response: %s\n", resp.text_response);
  
  // Cleanup
  typedef void (*ShutdownFunc)(void);
  ShutdownFunc shutdown = dlsym(handle, "plugin_shutdown");
  if (shutdown) {
    shutdown();
  }
  
  dlclose(handle);
  printf("All tests passed!\n");
  return 0;
}
```

Compile and run:

```bash
gcc -o test_plugin test_plugin.c -ldl -I/usr/local/include
./test_plugin
```

## Example Plugins

### Example 1: Simple Echo Plugin (No Dependencies)

```c
#include <instrument-server/plugin/PluginInterface.h>
#include <string.h>
#include <stdio.h>

static int g_initialized = 0;

PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {0};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "Echo Plugin", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "Echo", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description, "Simple echo plugin for testing", PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

int32_t plugin_initialize(const PluginConfig *config) {
  fprintf(stderr, "[Echo] Initializing for %s\n", config->instrument_name);
  g_initialized = 1;
  return 0;
}

int32_t plugin_execute_command(const PluginCommand *cmd, PluginResponse *resp) {
  strncpy(resp->command_id, cmd->id, PLUGIN_MAX_STRING_LEN - 1);
  strncpy(resp->instrument_name, cmd->instrument_name, PLUGIN_MAX_STRING_LEN - 1);

  if (!g_initialized) {
    resp->success = false;
    strncpy(resp->error_message, "Not initialized", PLUGIN_MAX_STRING_LEN - 1);
    return -1;
  }

  // Echo back the command verb
  resp->success = true;
  snprintf(resp->text_response, PLUGIN_MAX_PAYLOAD, "Echo: %s", cmd->verb);
  return 0;
}

void plugin_shutdown(void) {
  fprintf(stderr, "[Echo] Shutting down\n");
  g_initialized = 0;
}
```

**CMakeLists.txt:**

```cmake
cmake_minimum_required(VERSION 3.20)
project(EchoPlugin)

find_package(InstrumentServer REQUIRED)
add_instrument_plugin(echo_plugin SOURCES echo_plugin.c)
install(TARGETS echo_plugin LIBRARY DESTINATION lib/instrument-plugins)
```

### Example 2: Serial Port Plugin

```c
#include <instrument-server/plugin/PluginInterface.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

static int g_fd = -1;

PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {0};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "Serial Port Plugin", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta. version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "Serial", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description, "Serial port communication", PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

int32_t plugin_initialize(const PluginConfig *config) {
  // Parse port from config->connection_json
  const char *port = "/dev/ttyUSB0";  // TODO: Parse from JSON
  
  g_fd = open(port, O_RDWR | O_NOCTTY);
  if (g_fd < 0) {
    return -1;
  }
  
  // Configure serial port
  struct termios options;
  tcgetattr(g_fd, &options);
  cfsetispeed(&options, B9600);
  cfsetospeed(&options, B9600);
  options.c_cflag |= (CLOCAL | CREAD);
  options.c_cflag &= ~PARENB;
  options.c_cflag &= ~CSTOPB;
  options.c_cflag &= ~CSIZE;
  options.c_cflag |= CS8;
  tcsetattr(g_fd, TCSANOW, &options);
  
  return 0;
}

int32_t plugin_execute_command(const PluginCommand *cmd, PluginResponse *resp) {
  strncpy(resp->command_id, cmd->id, PLUGIN_MAX_STRING_LEN - 1);
  strncpy(resp->instrument_name, cmd->instrument_name, PLUGIN_MAX_STRING_LEN - 1);

  if (g_fd < 0) {
    resp->success = false;
    strncpy(resp->error_message, "Port not open", PLUGIN_MAX_STRING_LEN - 1);
    return -1;
  }

  // Send command
  char command[256];
  snprintf(command, sizeof(command), "%s\n", cmd->verb);
  write(g_fd, command, strlen(command));
  
  // Read response
  char buffer[1024];
  int n = read(g_fd, buffer, sizeof(buffer) - 1);
  
  if (n > 0) {
    buffer[n] = '\0';
    resp->success = true;
    strncpy(resp->text_response, buffer, PLUGIN_MAX_PAYLOAD - 1);
    return 0;
  }
  
  resp->success = false;
  strncpy(resp->error_message, "Read failed", PLUGIN_MAX_STRING_LEN - 1);
  return -1;
}

void plugin_shutdown(void) {
  if (g_fd >= 0) {
    close(g_fd);
    g_fd = -1;
  }
}
```

### Example 3: VISA Plugin

```c
#include <instrument-server/plugin/PluginInterface.h>
#include <visa. h>
#include <string. h>

static ViSession g_rm = VI_NULL;
static ViSession g_instr = VI_NULL;

PluginMetadata plugin_get_metadata(void) {
  PluginMetadata meta = {0};
  meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
  strncpy(meta.name, "VISA Plugin", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.protocol_type, "VISA", PLUGIN_MAX_STRING_LEN - 1);
  strncpy(meta.description, "VISA/SCPI instrument driver", PLUGIN_MAX_STRING_LEN - 1);
  return meta;
}

int32_t plugin_initialize(const PluginConfig *config) {
  // Parse VISA address from config->connection_json
  const char *address = "TCPIP:: 192.168.1.1:: INSTR";  // TODO: Parse from JSON
  
  ViStatus status = viOpenDefaultRM(&g_rm);
  if (status < VI_SUCCESS) {
    return -1;
  }
  
  status = viOpen(g_rm, address, VI_NULL, VI_NULL, &g_instr);
  if (status < VI_SUCCESS) {
    viClose(g_rm);
    return -2;
  }
  
  return 0;
}

int32_t plugin_execute_command(const PluginCommand *cmd, PluginResponse *resp) {
  strncpy(resp->command_id, cmd->id, PLUGIN_MAX_STRING_LEN - 1);
  strncpy(resp->instrument_name, cmd->instrument_name, PLUGIN_MAX_STRING_LEN - 1);

  if (g_instr == VI_NULL) {
    resp->success = false;
    strncpy(resp->error_message, "VISA not initialized", PLUGIN_MAX_STRING_LEN - 1);
    return -1;
  }

  // Send SCPI command
  viPrintf(g_instr, "%s\n", cmd->verb);
  
  // If expects response, read it
  if (cmd->expects_response) {
    char buffer[256];
    ViUInt32 ret_count;
    ViStatus status = viScanf(g_instr, "%t", &ret_count, buffer);
    
    if (status >= VI_SUCCESS) {
      resp->success = true;
      strncpy(resp->text_response, buffer, PLUGIN_MAX_PAYLOAD - 1);
      return 0;
    }
    
    resp->success = false;
    resp->error_code = status;
    return status;
  }
  
  resp->success = true;
  return 0;
}

void plugin_shutdown(void) {
  if (g_instr != VI_NULL) {
    viClose(g_instr);
    g_instr = VI_NULL;
  }
  if (g_rm != VI_NULL) {
    viClose(g_rm);
    g_rm = VI_NULL;
  }
}
```

**CMakeLists.txt for VISA plugin:**

```cmake
cmake_minimum_required(VERSION 3.20)
project(VISAPlugin)

find_package(InstrumentServer REQUIRED)

# Find VISA library
find_library(VISA_LIBRARY NAMES visa visa64 PATHS /usr/local/vxipnp/linux64/lib)

add_instrument_plugin(visa_plugin
  SOURCES visa_plugin.c
  LINK_LIBRARIES ${VISA_LIBRARY}
)

install(TARGETS visa_plugin LIBRARY DESTINATION lib/instrument-plugins)
```

## Handling Large Data in Plugins

### When to Use Data Buffers

Use data buffers when your plugin returns:

- Oscilloscope waveforms (>1000 points)
- Spectrum analyzer traces
- Camera images
- Large measurement arrays
- Any data >1KB

### Creating Data Buffers in C Plugins

```c
#include <instrument-server/ipc/DataBufferManager_C.h>

int32_t plugin_execute_command(const PluginCommand *cmd, PluginResponse *resp) {
  // ... execute command and get data ...
  
  // Example: Large waveform data
  size_t num_points = 10000;
  float *waveform = get_waveform_from_instrument();
  
  // Create buffer
  char buffer_id[PLUGIN_MAX_STRING_LEN];
  int result = data_buffer_create(
      cmd->instrument_name,
      cmd->id,
      0,  // 0 = FLOAT32, 1 = FLOAT64, 2 = INT32, etc.
      num_points,
      waveform,
      buffer_id
  );
  
  if (result == 0) {
    // Success - set response
    resp->success = true;
    resp->has_large_data = true;
    strncpy(resp->data_buffer_id, buffer_id, PLUGIN_MAX_STRING_LEN - 1);
    resp->data_element_count = num_points;
    resp->data_type = 0;  // FLOAT32
    
    snprintf(resp->text_response, PLUGIN_MAX_PAYLOAD,
             "Waveform data in buffer %s", buffer_id);
  } else {
    resp->success = false;
    strncpy(resp->error_message, "Failed to create buffer",
            PLUGIN_MAX_STRING_LEN - 1);
  }
  
  free(waveform);
  return result;
}
```

### Data Type Codes

```c
// Data type enum values for data_buffer_create()
#define DATA_TYPE_FLOAT32  0
#define DATA_TYPE_FLOAT64  1
#define DATA_TYPE_INT32    2
#define DATA_TYPE_INT64    3
#define DATA_TYPE_UINT32   4
#define DATA_TYPE_UINT64   5
#define DATA_TYPE_UINT8    6
```

### Buffer Lifecycle

1. **Plugin creates buffer** - Calls `data_buffer_create()`
2. **Server receives buffer ID** - In `PluginResponse`
3. **Lua accesses buffer** - Via `get_buffer()`
4. **User exports data** - Calls `buffer:export_csv()` or `buffer:export_binary()`
5. **User releases buffer** - Calls `buffer:release()`
6. **Auto cleanup** - Buffer freed when ref count reaches 0

### Linking to DataBufferManager

In your plugin's `CMakeLists.txt`:

```cmake
target_link_libraries(your_plugin
  PRIVATE
    InstrumentServerCore  # Provides DataBufferManager C API
)
```

### Example: Oscilloscope Plugin

```c
int32_t get_waveform(const PluginCommand *cmd, PluginResponse *resp) {
  // Query number of points
  uint32_t num_points;
  visa_query(g_session, "WAV: POIN?", &num_points);
  
  // Allocate temporary buffer
  float *waveform = malloc(num_points * sizeof(float));
  
  // Read waveform data from instrument
  visa_read_binary(g_session, waveform, num_points);
  
  // Create shared buffer
  char buffer_id[PLUGIN_MAX_STRING_LEN];
  if (data_buffer_create(cmd->instrument_name, cmd->id,
                        DATA_TYPE_FLOAT32, num_points,
                        waveform, buffer_id) == 0) {
    resp->success = true;
    resp->has_large_data = true;
    strncpy(resp->data_buffer_id, buffer_id, PLUGIN_MAX_STRING_LEN - 1);
    resp->data_element_count = num_points;
    resp->data_type = DATA_TYPE_FLOAT32;
  }
  
  free(waveform);
  return 0;
}
```

## See Also

- [Architecture](ARCHITECTURE.md) - System design
- [CLI Usage](CLI_USAGE.md) - Testing plugins
- [Main README](../README.md) - Getting started and installation
