
# Instrument Script Server Architecture

<!--toc:start-->
- [Instrument Script Server Architecture](#instrument-script-server-architecture)
  - [Overview](#overview)
  - [Key Components](#key-components)
    - [1. Main Server Process (`instrument-server`)](#1-main-server-process-instrument-server)
    - [2. Worker Processes (`instrument-worker`)](#2-worker-processes-instrument-worker)
    - [3. Plugins (`.so`/`.dll`)](#3-plugins-sodll)
  - [IPC Protocol](#ipc-protocol)
    - [Message Queue Architecture](#message-queue-architecture)
    - [Message Types](#message-types)
    - [Message Format](#message-format)
    - [Plugin API](#plugin-api)
  - [Lua Runtime Context](#lua-runtime-context)
  - [Configuration](#configuration)
    - [Instrument Config](#instrument-config)
    - [API Definition](#api-definition)
  - [Process Lifecycle](#process-lifecycle)
  - [Error Handling](#error-handling)
  - [Performance Considerations](#performance-considerations)
  - [Security](#security)
<!--toc:end-->

## Overview

The Instrument Script Server is a modular, process-isolated system for controlling scientific instruments. It uses a plugin-based architecture with IPC (Inter-Process Communication) for robustness and parallelism.

## Key Components

### 1. Main Server Process (`instrument-server`)

- Loads instrument configurations
- Creates worker processes for each instrument
- Executes Lua measurement scripts
- Provides RuntimeContext API to scripts

### 2. Worker Processes (`instrument-worker`)

- One worker per instrument
- Isolated address space (crash doesn't affect others)
- Loads instrument-specific plugin
- Communicates with main process via Boost.Interprocess message queues

### 3. Plugins (`.so`/`.dll`)

- Implement instrument-specific communication logic
- Built by users for custom instruments
- Conform to C API defined in `PluginInterface.h`
- Can be written in C or C++

## IPC Protocol

### Message Queue Architecture

```Code
┌────────────────────┐        Request Queue        ┌────────────────────┐
│   Main Process     │ ──────────────────────────> │  Worker Process    │
│  InstrumentWorker  │                             │  Plugin Loader     │
│      Proxy         │ <────────────────────────── │  Plugin            │
└────────────────────┘       Response Queue        │  (VISA/DAQmx/      │
                                                   │   Custom)          │
                                                   └────────────────────┘
```

### Message Types

- **COMMAND**: Execute instrument command
- **RESPONSE**: Result from command execution
- **HEARTBEAT**: Worker alive signal
- **SHUTDOWN**: Graceful shutdown request
- **ERROR**: Fatal error notification

### Message Format

```c
struct IPCMessage {
    Type type;
    uint64_t id;            // For request/response matching
    uint32_t payload_size;
    char payload[8192];     // JSON-serialized command/response
};
```

### Plugin API

Plugins must implement 4 functions:

```C
PluginMetadata plugin_get_metadata(void);
int32_t plugin_initialize(const PluginConfig* config);
int32_t plugin_execute_command(const PluginCommand* cmd, PluginResponse* resp);
void plugin_shutdown(void);
```

See [PluginInterface.h](../include/instrument-server/plugin/PluginInterface.h) for full details.

## Lua Runtime Context

Measurement scripts interact with instruments via RuntimeContext:

```Lua

-- context.call(instrument. command, params)
context.call("DMM1.MEASURE_VOLTAGE", {range = 10.0})

-- Parallel execution
context.parallel(function()
    context.call("SCOPE1.SET_SAMPLE_RATE", {rate = 1e9})
    context.call("SCOPE2.SET_SAMPLE_RATE", {rate = 1e9})
end)

-- Logging
context.log("Starting measurement")
```

Three context types:

- RuntimeContext_DCGetSet: DC voltage get/set
- RuntimeContext_1DWaveform: 1D parameter sweep
- RuntimeContext_2DWaveform: 2D parameter sweep

## Configuration

### Instrument Config

```YAML
name: DMM1
api_ref: examples/instrument-apis/agi_34401a.yaml
connection:
  type:  VISA
  address: "TCPIP:: 192.168.0.1:: INSTR"
  plugin: "/usr/local/lib/instrument-plugins/libvisa_plugin.so"  # Optional, auto-discovered
```

### API Definition

```YAML
api_version: "1.0.0"
instrument:
  vendor: "Agilent"
  model: "34401A"
protocol:
  type: "VISA"
commands:
  MEASURE_VOLTAGE:
    template: "MEAS:VOLT:DC?"
    description: "Measure DC voltage"
    parameters:  []
    returns: float
```

## Process Lifecycle

1. **Startup**
   - Server loads configs
   - Creates IPC queues
   - Spawns worker processes
   - Workers load plugins
   - Plugins initialize

1. **Operation**
   - Lua script calls context.call()
   - Proxy serializes command → IPC
   - Worker receives → Plugin executes
   - Plugin returns response → IPC
   - Proxy deserializes → Future resolved

1. **Shutdown**
   - Server sends SHUTDOWN messages
   - Workers cleanup plugins
   - Processes exit
   - IPC queues destroyed

## Error Handling

- Worker Crash: Detected by missing heartbeats, pending commands fail with error
- IPC Timeout: Commands fail after timeout, logged
- Plugin Error: Returned in CommandResponse, doesn't crash worker
- Invalid Config: Detected at startup, server exits with error

## Performance Considerations

- Shared Memory: Fast IPC (sub-microsecond latency)
- Process Isolation: True parallel execution across instruments
- Zero-Copy: Large data (waveforms) can use shared memory regions (future enhancement)

## Security

- Process Boundaries: Worker crash doesn't affect other instruments
- Plugin Sandboxing: Future: use seccomp/AppArmor to restrict plugin capabilities
- Config Validation: YAML schema validation before instrument creation
