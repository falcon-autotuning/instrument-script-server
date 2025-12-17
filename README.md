# Instrument Script Schema

<!--toc:start-->
- [Instrument Script Schema](#instrument-script-schema)
  - [Features](#features)
  - [Quick Start](#quick-start)
    - [Installation](#installation)
    - [Run Example](#run-example)
  - [Architecture](#architecture)
  - [For Users: Using Existing Plugins](#for-users-using-existing-plugins)
    - [Install the Server](#install-the-server)
    - [Create Instrument Config](#create-instrument-config)
    - [Write Measurement Script](#write-measurement-script)
    - [Run](#run)
  - [For Developers: Creating Custom Plugins](#for-developers-creating-custom-plugins)
    - [Setup Development Environment](#setup-development-environment)
    - [Create Plugin Source](#create-plugin-source)
    - [Create CMakeLists.txt](#create-cmakeliststxt)
    - [Build and Install](#build-and-install)
    - [Create API Definition](#create-api-definition)
    - [Create Instrument Config](#create-instrument-config)
    - [Test](#test)
  - [Documentation](#documentation)
  - [API Reference](#api-reference)
    - [Lua RuntimeContext API](#lua-runtimecontext-api)
    - [C Plugin API](#c-plugin-api)
  - [Examples](#examples)
  - [Requirements](#requirements)
    - [Runtime](#runtime)
    - [Development (for building plugins)](#development-for-building-plugins)
    - [Optional](#optional)
  - [Building from Source](#building-from-source)
  - [Troubleshooting](#troubleshooting)
    - [Plugin won't load](#plugin-wont-load)
    - [Worker process crashes](#worker-process-crashes)
    - [IPC issues](#ipc-issues)
    - [License](#license)
    - [Support](#support)
<!--toc:end-->

Core schema definitions, validation, and **modular instrument server** for scientific instrument control with process isolation and plugin architecture.

## Features

- ✅ **Process-isolated workers**: Each instrument runs in its own process
- ✅ **Plugin architecture**: Zero hardcoded instruments, 100% extensible
- ✅ **IPC via Boost.Interprocess**: Fast, reliable message queues
- ✅ **Built-in VISA support**: Ships with VISA/SCPI plugin
- ✅ **Cross-platform**: Linux and Windows support
- ✅ **Lua scripting**: Measurement scripts with 3 runtime contexts
- ✅ **Schema validation**:  YAML config validation against JSON schemas
- ✅ **Rich logging**: Per-instrument, per-command tracing with spdlog

## Quick Start

### Installation

```bash
# Build from source
mkdir build && cd build
cmake ..  -DCMAKE_BUILD_TYPE=Release
cmake --build .  -- -j$(nproc)
sudo cmake --install . 
```

### Run Example

```bash
# Validate config
instrument-setup validate --config examples/instrument-configurations/agi_34401_config.yaml

# Test instrument
instrument-setup test \
  --config examples/instrument-configurations/agi_34401_config. yaml \
  --command MEASURE_VOLTAGE

# Run measurement script
instrument-server \
  --config examples/instrument-configurations/agi_34401_config.yaml \
  --script examples/scripts/measurement_1d_scalar.lua
```

## Architecture

```Code
┌─────────────────────────────────────────────────────────┐
│               instrument-server (main process)          │
│  ┌───────────────────────────────────────────────────┐  │
│  │  Lua Runtime + RuntimeContext                     │  │
│  └─────────────────────┬─────────────────────────────┘  │
│                        │ IPC (message queues)           │
│  ┌─────────────────────┼─────────────────────────────┐  │
│  │  InstrumentWorkerProxy (per instrument)           │  │
│  └─────────────────────┬─────────────────────────────┘  │
└────────────────────────┼────────────────────────────────┘
                         │
        ┌────────────────┼───────────────┐
        │                │               │
  ┌─────▼──────┐   ┌─────▼──────┐  ┌─────▼──────┐
  │  Worker 1  │   │  Worker 2  │  │  Worker 3  │
  │  (Process) │   │  (Process) │  │  (Process) │
  │  Plugin A  │   │  Plugin B  │  │  Plugin C  │
  └────────────┘   └────────────┘  └────────────┘
```

## For Users: Using Existing Plugins

1. ### Install the Server

Download and extract the binary release for your platform:

```bash
# Linux
tar xzf instrument-server-v1.0-linux-x86_64.tar.gz
cd instrument-server
sudo ./install.sh

# Windows
# Extract zip and run install.bat
```

1. ### Create Instrument Config

```YAML
# my_dmm.yaml
name: DMM1
api_ref: /usr/local/share/instrument-server/apis/agi_34401a.yaml
connection:
  type:  VISA
  address: "TCPIP::192.168.1.100:: INSTR"
```

1. ### Write Measurement Script

```Lua
-- my_measurement.lua
local ctx = context  -- Provided by server

ctx.log("Starting measurement")

-- Set voltage
ctx.call("DMM1.SET_VOLTAGE", {voltage = 3.3})

-- Measure
local result = ctx.call("DMM1.MEASURE_VOLTAGE", {})
ctx.log("Measured:  " .. result.value ..  " V")
```

1. ### Run

```bash

instrument-server \
  --config my_dmm.yaml \
  --script my_measurement.lua \
  --log-level debug
```

## For Developers: Creating Custom Plugins

1. ### Setup Development Environment

```bash
# Install development headers
sudo apt-get install libboost-dev libyaml-cpp-dev libfmt-dev libspdlog-dev lua5.4 liblua5.4-dev

# Install instrument-server dev package
sudo apt-get install instrument-server-dev

# Or build from source (see above)
```

1. ### Create Plugin Source

```C
// my_instrument_plugin.c
# include <instrument_script/plugin/PluginInterface.h>
# include <my_instrument_sdk. h>  // Your instrument's SDK

static HANDLE g_device = NULL;

PluginMetadata plugin_get_metadata(void) {
    PluginMetadata meta = {0};
    meta.api_version = INSTRUMENT_PLUGIN_API_VERSION;
    strncpy(meta.name, "My Instrument", PLUGIN_MAX_STRING_LEN - 1);
    strncpy(meta.version, "1.0.0", PLUGIN_MAX_STRING_LEN - 1);
    strncpy(meta.protocol_type, "MyInstrument", PLUGIN_MAX_STRING_LEN - 1);
    return meta;
}

int32_t plugin_initialize(const PluginConfig* config) {
    // Parse config->connection_json
    // Open connection using your SDK
    g_device = MySDK_Open(".. .");
    return (g_device != NULL) ? 0 : -1;
}

int32_t plugin_execute_command(const PluginCommand*command, PluginResponse* response) {
    // Fill response metadata
    strncpy(response->command_id, command->id, PLUGIN_MAX_STRING_LEN - 1);
    strncpy(response->instrument_name, command->instrument_name, PLUGIN_MAX_STRING_LEN - 1);

    // Handle commands
    if (strcmp(command->verb, "MEASURE") == 0) {
        double value = MySDK_Measure(g_device);
        response->success = true;
        response->return_value. type = PARAM_TYPE_DOUBLE;
        response->return_value.value. d_val = value;
        return 0;
    }
    
    response->success = false;
    return -1;
}

void plugin_shutdown(void) {
    if (g_device) {
        MySDK_Close(g_device);
        g_device = NULL;
    }
}
```

1. ### Create CMakeLists.txt

```CMake
cmake_minimum_required(VERSION 3.20)
project(MyInstrumentPlugin VERSION 1.0.0)

find_package(InstrumentServer REQUIRED)

add_instrument_plugin(my_instrument_plugin
    SOURCES my_instrument_plugin.c
    LINK_LIBRARIES my_instrument_sdk
)

install(TARGETS my_instrument_plugin
    LIBRARY DESTINATION lib/instrument-plugins
)
```

1. ### Build and Install

```bash
mkdir build && cd build
cmake ..  -DCMAKE_PREFIX_PATH=/usr/local
cmake --build .
sudo cmake --install .
```

1. ### Create API Definition

```YAML
# my_instrument_api.yaml
api_version: "1.0.0"
instrument:
  vendor: "Acme Corp"
  model: "Model 1234"
protocol:
  type: "MyInstrument"
commands:
  MEASURE:
    description: "Measure voltage"
    parameters:  []
    returns: float
```

1. ### Create Instrument Config

```YAML
# my_instrument_config.yaml
name: ACME1
api_ref: /path/to/my_instrument_api.yaml
connection:
  type: MyInstrument
  address: "192.168.1.200"
  plugin:  "/usr/local/lib/instrument-plugins/my_instrument_plugin.so"
  ```

1. ### Test

```bash
instrument-setup test \
  --config my_instrument_config.yaml \
  --command MEASURE
```

See [docs/PLUGIN_DEVELOPMENT.md](/docs/PLUGIN_DEVELOPMENT.md) for full documentation.

## Documentation

- [Architecture](/docs/ARCHITECTURE.md) - System design and IPC protocol
- [Plugin Development](/docs/PLUGIN_DEVELOPMENT.md) - How to create custom plugins
- [IPC Protocol](/docs/IPC_PROTOCOL.md) - Message format and flow specification

## API Reference

### Lua RuntimeContext API

```Lua
-- Call instrument command
result = context.call("INSTRUMENT.COMMAND", {param1 = value1, ... })

-- Parallel execution
context.parallel(function()
    context.call("INST1.CMD", {})
    context.call("INST2.CMD", {})
end)

-- Logging
context.log("Message")
```

### C Plugin API

```C

PluginMetadata plugin_get_metadata(void);
int32_t plugin_initialize(const PluginConfig*config);
int32_t plugin_execute_command(const PluginCommand* cmd, PluginResponse* resp);
void plugin_shutdown(void);
```

## Examples

- [examples/plugins/simple_serial](/examples/plugins/simple_serial/README.md) - Minimal serial instrument plugin
- [examples/scripts] - Lua measurement scripts
- [examples/instrument-apis] - API definitions for common instruments

## Requirements

### Runtime

- Boost >= 1.74 (system, filesystem, interprocess)
- Lua >= 5.4
- yaml-cpp >= 0.7
- fmt >= 8.0
- spdlog >= 1.9

### Development (for building plugins)

- CMake >= 3.20
- C11/C++20 compiler
- instrument-server development headers

### Optional

- NI-VISA (for VISA instruments)
- NI-DAQmx (for National Instruments DAQ devices)
- Google Test (for tests)

## Building from Source

```bash
git clone <https://github.com/falcon-autotuning/instrument-script-schema>. git
cd instrument-script-schema
mkdir build && cd build
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=ON \
  -DBUILD_EXAMPLES=ON
cmake --build .  -- -j$(nproc)
ctest  # Run tests
sudo cmake --install .
```

## Troubleshooting

### Plugin won't load

```bash
# Check dependencies
ldd /path/to/plugin.so

# Check symbols
nm -D /path/to/plugin.so | grep plugin_

# Test manually
instrument-setup test --config my_config.yaml --command TEST
```

### Worker process crashes

```bash
# Check worker logs
tail -f worker_<instrument_name>.log

# Run worker directly for debugging
instrument-worker --instrument TEST --plugin ./my_plugin.so
```

### IPC issues

```bash
# Linux:  check message queues
ls -la /dev/mqueue/

# Clean up stale queues
rm /dev/mqueue/instrument_*

# Windows: restart server (queues auto-cleanup)
```

### License

Mozilla Public License 2.0 - see [LICENSE](LICENSE)

### Support

- Issues: <https://github.com/falcon-autotuning/instrument-script-schema/issues>
- Discussions: <https://github.com/falcon-autotuning/instrument-script-schema/discussions>
