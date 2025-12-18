# Falcon Instrument Script Server

A modular, process-isolated system for controlling scientific instruments in laboratory automation environments.

## Overview

The Instrument Script Server provides:

- **Process Isolation**: Each instrument runs in a separate worker process for fault tolerance
- **Plugin Architecture**: Instrument drivers as loadable plugins (VISA, serial, custom SDKs)
- **Lua Scripting**: High-level measurement scripts with runtime contexts
- **Synchronization**: Parallel execution with precise timing coordination across instruments
- **Cross-Platform**: Works on Linux and Windows

## Table of Contents

- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [Installation](#installation)
- [Usage](#usage)
  - [Starting the Daemon](#starting-the-daemon)
  - [Managing Instruments](#managing-instruments)
  - [Running Measurements](#running-measurements)
- [Writing Measurement Scripts](#writing-measurement-scripts)
- [Plugin Development](#plugin-development)
- [Configuration](#configuration)
- [Testing](#testing)
- [Troubleshooting](#troubleshooting)
- [License](#license)

## Quick Start

```bash
# 1. Build and install
mkdir build && cd build
cmake ..
cmake --build . 
sudo cmake --install .

# 2. Start the server daemon
instrument-server daemon start

# 3. Start instruments
instrument-server start configs/dac1.yaml
instrument-server start configs/dmm1.yaml

# 4. Run a measurement
instrument-server measure dc my_measurement.lua

# 5. Check status
instrument-server list
instrument-server status DMM1

# 6. Shutdown
instrument-server daemon stop
```

## Architecture

```
┌────────────────────────────────────────────────────────┐
│                   instrument-server                    │
│                    (Main Process)                      │
│                                                        │
│  ┌────────────────┐  ┌───────────────────┐             │
│  │ ServerDaemon   │  │ InstrumentRegistry│             │
│  │  - Lifecycle   │  │  - Manages proxies│             │
│  └────────────────┘  └───────────────────┘             │
│                                                        │
│  ┌────────────────────────────────────────────────┐    │
│  │ SyncCoordinator                                │    │
│  │  - Manages parallel execution barriers         │    │
│  └────────────────────────────────────────────────┘    │
└────────────────────────────────────────────────────────┘
                          ↓ IPC (Shared Memory)
┌────────────────────────────────────────────────────────┐
│            Worker Processes (Per Instrument)           │
│                                                        │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │ instrument-  │  │ instrument-  │  │ instrument-  │  │
│  │ worker       │  │ worker       │  │ worker       │  │
│  │ (DMM)        │  │ (DAC)        │  │ (Scope)      │  │
│  │              │  │              │  │              │  │
│  │ ┌──────────┐ │  │ ┌──────────┐ │  │ ┌──────────┐ │  │
│  │ │ Plugin   │ │  │ │ Plugin   │ │  │ │ Plugin   │ │  │
│  │ └──────────┘ │  │ └──────────┘ │  │ └──────────┘ │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
└────────────────────────────────────────────────────────┘
                          ↓
┌────────────────────────────────────────────────────────┐
│                  Hardware Instruments                  │
│  VISA/SCPI • Serial • USB • Network • Native SDKs      │
└────────────────────────────────────────────────────────┘
```

### Key Components

- **ServerDaemon**: Background process managing instrument lifecycle
- **InstrumentRegistry**: Tracks running instruments and their proxies
- **InstrumentWorkerProxy**: IPC client for each instrument
- **Worker Process**:  Isolated process running instrument plugin
- **SyncCoordinator**:  Coordinates parallel execution across instruments
- **PluginLoader**: Dynamically loads instrument drivers (.  so/. dll)

## Installation

### Dependencies

Required:

- CMake 3.20+
- C++17 compiler
- Lua 5.3+ or LuaJIT
- sol2 (header-only, Lua C++ bindings)
- spdlog (logging)
- nlohmann_json (JSON parsing)
- yaml-cpp (YAML parsing)
- Google Test (for testing)

Optional:

- NI-VISA (for VISA instruments)

### Ubuntu/Debian

```bash
sudo apt-get install cmake g++ liblua5.3-dev libyaml-cpp-dev \
                     libspdlog-dev nlohmann-json3-dev libgtest-dev

# sol2 (header-only)
sudo apt-get install sol-lua  # or download from https://github.com/ThePhD/sol2
```

### Build

```bash
git clone https://github.com/falcon-autotuning/instrument-script-server.git
cd instrument-script-server
mkdir build && cd build
cmake ..
cmake --build .  -j$(nproc)
sudo cmake --install .
```

### Verify Installation

```bash
instrument-server --help
instrument-server plugins
```

## Usage

### Starting the Daemon

The server daemon manages the instrument registry and must be running before starting instruments:

```bash
# Start daemon (runs in background)
instrument-server daemon start

# Check daemon status
instrument-server daemon status

# Stop daemon (stops all instruments)
instrument-server daemon stop
```

**Note**: The daemon must be running for all instrument operations.

### Managing Instruments

#### Start Instruments

```bash
# Start from configuration file
instrument-server start configs/keithley_dmm. yaml

# Start with custom plugin
instrument-server start configs/custom_instrument.yaml --plugin ./my_plugin.so

# Start multiple instruments
instrument-server start configs/dac1.yaml
instrument-server start configs/dac2.yaml
instrument-server start configs/dmm1.yaml
```

#### List Running Instruments

```bash
instrument-server list
```

Output:

```
Running instruments:
  DAC1 [RUNNING]
  DAC2 [RUNNING]
  DMM1 [RUNNING]
```

#### Check Instrument Status

```bash
instrument-server status DMM1
```

Output:

```
Instrument:  DMM1
  Status:  RUNNING
  Commands sent: 150
  Commands completed: 148
  Commands failed: 0
  Commands timeout: 2
```

#### Stop Instrument

```bash
instrument-server stop DMM1
```

### Running Measurements

Measurements are executed via Lua scripts with a specified measurement type:

```bash
# DC measurement
instrument-server measure dc my_dc_measurement.lua

# 1D waveform sweep
instrument-server measure waveform1d sweep_gate.lua

# 2D waveform sweep
instrument-server measure waveform2d stability_diagram.lua
```

**Measurement Types:**

- `dc`: DC get/set operations with simple voltage control
- `waveform1d`: 1D voltage sweeps with buffered acquisition
- `waveform2d`: 2D voltage sweeps (e.g., stability diagrams)

### Testing Single Commands

```bash
# Test identity query
instrument-server test configs/dmm. yaml IDN

# Test with parameters
instrument-server test configs/dac.yaml SET_VOLTAGE channel=1 voltage=5. 0

# Test with custom plugin
instrument-server test configs/custom. yaml MEASURE --plugin ./custom. so
```

### Plugin Discovery

```bash
# List available plugins
instrument-server plugins

# Discover plugins in custom directories
instrument-server discover /opt/custom-plugins ./local-plugins
```

## Writing Measurement Scripts

Measurement scripts are written in Lua and interact with instruments via the `context` object.

### Example:  DC Measurement

```lua name=examples/dc_measurement.lua
-- DC measurement script
-- Run with: instrument-server measure dc dc_measurement.lua

-- Configure measurement
context.sampleRate = 1000  -- Hz
context.numPoints = 100

-- Set output voltages
context.setVoltages = {
    ["DAC1"] = 2.5,
    ["DAC2"] = 1.0
}

-- Configure getters and setters
local dac1 = InstrumentTarget()
dac1.id = "DAC1"
dac1.channel = 1

local dmm1 = InstrumentTarget()
dmm1.id = "DMM1"

context.setters = {dac1}
context.getters = {dmm1}

-- Perform sweep
for voltage = 0, 5, 0.1 do
    -- Set voltage on DAC1
    context: call("DAC1:1. SetVoltage", voltage)
    
    -- Wait for settling
    os.execute("sleep 0.01")
    
    -- Measure
    local result = context:call("DMM1.Measure")
    
    -- Log result
    context:log(string.format("V_set=%.2f, V_meas=%.6f", voltage, result))
end

context: log("Measurement complete")
```

### Example: Parallel Execution

```lua name=examples/parallel_measurement.lua
-- Parallel execution with synchronization
-- Run with: instrument-server measure dc parallel_measurement.lua

-- Set multiple voltages simultaneously
context: parallel(function()
    context: call("DAC1:1.SetVoltage", 1.5)
    context:call("DAC2:1.SetVoltage", 2.0)
    context:call("DAC3:1.SetVoltage", 0.5)
end)

-- All DACs are now at their target voltages
context:log("All voltages set")

-- Measure from multiple instruments simultaneously
local results = {}
context:parallel(function()
    results. dmm1 = context:call("DMM1.Measure")
    results.dmm2 = context:call("DMM2.Measure")
end)

context:log(string.format("DMM1: %.6f, DMM2: %.6f", results.dmm1, results.dmm2))
```

### Example: 1D Waveform

```lua name=examples/waveform1d_measurement.lua
-- 1D waveform measurement
-- Run with: instrument-server measure waveform1d waveform1d_measurement.lua

context.sampleRate = 1e6  -- 1 MHz
context.numPoints = 1000
context.numSteps = 50

-- Configure voltage domains
context.setVoltageDomains = {
    ["DAC1:1"] = {min = -0.5, max = 0.5}
}

-- Setup targets
local dac1 = InstrumentTarget()
dac1.id = "DAC1"
dac1.channel = 1

local dmm1 = InstrumentTarget()
dmm1.id = "DMM1"

context.bufferedSetters = {dac1}
context.bufferedGetters = {dmm1}

-- The runtime will automatically generate sweep and collect data
context:log("Starting 1D sweep")

-- Sweep is executed by the runtime based on configured domains
-- Results are collected automatically

context:log("Sweep complete")
```

### RuntimeContext API

All scripts have access to a `context` object with the following methods:

#### Common Methods (All Contexts)

```lua
-- Call instrument command
result = context:call("InstrumentID. CommandVerb", arg1, arg2, ...)

-- Call with channel
result = context:call("InstrumentID: 3.CommandVerb", value)

-- Execute commands in parallel (synchronized)
context:parallel(function()
    context:call("DAC1.Set", 5.0)
    context:call("DAC2.Set", 3.0)
end)

-- Log message
context:log("Message text")
```

#### RuntimeContext_DCGetSet

```lua
context. sampleRate = 1000       -- Sampling rate (Hz)
context.numPoints = 100          -- Number of points
context.setVoltages = {          -- Voltage settings
    ["DAC1"] = 5.0,
    ["DAC2"] = 3.0
}
context.getters = {target1}      -- Instruments to read from
context.setters = {target2}      -- Instruments to write to
```

#### RuntimeContext_1DWaveform

```lua
context.sampleRate = 1e6         -- Sampling rate (Hz)
context.numPoints = 1000         -- Points per step
context.numSteps = 50            -- Number of steps
context.setVoltageDomains = {    -- Sweep ranges
    ["DAC1:1"] = {min = -1.0, max = 1.0}
}
context.bufferedGetters = {... }  -- Buffered acquisition
context.bufferedSetters = {...}  -- Buffered output
```

#### RuntimeContext_2DWaveform

```lua
context.sampleRate = 1e6
context.numPoints = 1000
context.numXSteps = 20
context.numYSteps = 20
context.setXVoltageDomains = {
    ["DAC1:1"] = {min = -1.0, max = 1.0}
}
context.setYVoltageDomains = {
    ["DAC2:1"] = {min = -0.5, max = 0.5}
}
context.bufferedXSetters = {...}
context.bufferedYSetters = {...}
context.bufferedGetters = {...}
```

## Plugin Development

After installing InstrumentServer, you can easily create plugins for your instruments.
Plugins are shared libraries (.so on Linux, .dll on Windows) that implement the instrument driver interface.

### Plugin Interface

```c
// Required functions
PluginMetadata plugin_get_metadata(void);
int32_t plugin_initialize(const PluginConfig *config);
int32_t plugin_execute_command(const PluginCommand *cmd, PluginResponse *resp);
void plugin_shutdown(void);
```

### Quick Plugin Creation

```bash
# 1. Create plugin directory
mkdir my_instrument_plugin && cd my_instrument_plugin

# 2. Create CMakeLists.txt
cat > CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.20)
project(MyInstrumentPlugin)

find_package(InstrumentServer REQUIRED)

add_instrument_plugin(my_instrument_plugin
  SOURCES my_plugin.c
)

install(TARGETS my_instrument_plugin
  LIBRARY DESTINATION lib/instrument-plugins
)
EOF

# 3. Create your plugin (my_plugin.c)
# See docs/PLUGIN_DEVELOPMENT.md for examples

# 4. Build and install
mkdir build && cd build
cmake ..
cmake --build .
sudo cmake --install . 

# 5. Test it
instrument-server test my_config.yaml IDN
```

For detailed plugin development guide, see [docs/PLUGIN_DEVELOPMENT.md](docs/PLUGIN_DEVELOPMENT.md).

## Configuration

### Instrument Configuration

```yaml name=configs/example_instrument.yaml
name: DMM1
api_ref: apis/keithley_2400.yaml
connection:
  type: VISA
  address: "TCPIP:: 192.168.1.100:: INSTR"
  timeout: 5000
```

### API Definition

```yaml name=apis/keithley_2400.yaml
protocol: 
  type:  VISA

commands:
  IDN:
    description: "Query instrument identification"
    template: "*IDN?"
    response_type: string

  MEASURE:
    description: "Measure DC voltage"
    template: ": MEAS: VOLT:DC?"
    response_type: double

  SET_VOLTAGE:
    description: "Set output voltage"
    template: ": SOUR:VOLT {voltage}"
    params:
      voltage:
        type: double
        required: true
```

## Testing

### Running Tests

```bash
cd build

# All tests
ctest

# Or use make targets
make test

# Unit tests only (fast)
make test_unit

# Integration tests only
make test_integration

# Performance benchmarks
make test_perf
```

### Test Organization

```
tests/
├── unit/               # Fast, isolated unit tests
│   ├── test_sync_coordinator.cpp
│   ├── test_serialization.cpp
│   ├── test_ipc_queue.cpp
│   └── ... 
├── integration/        # Multi-component integration tests
│   ├── test_daemon_lifecycle.cpp
│   ├── test_parallel_execution.cpp
│   └── ...
├── performance/        # Performance benchmarks
│   ├── test_ipc_throughput.cpp
│   └── test_sync_overhead.cpp
└── mocks/             # Mock plugins for testing
    └── mock_plugin.cpp
```

### Writing Tests

```cpp
#include <gtest/gtest. h>
#include "instrument-server/server/SyncCoordinator.hpp"

TEST(SyncCoordinator, BasicBarrier) {
  SyncCoordinator sync;
  sync.register_barrier(1, {"A", "B", "C"});
  
  EXPECT_FALSE(sync.handle_ack(1, "A"));
  EXPECT_FALSE(sync. handle_ack(1, "B"));
  EXPECT_TRUE(sync.handle_ack(1, "C"));  // Last one completes
}
```

## Troubleshooting

### Daemon Issues

**Problem**: `Error: Server daemon is not running`

```bash
# Check daemon status
instrument-server daemon status

# Start daemon
instrument-server daemon start

# Check for stale PID files
ls -la /tmp/instrument-server-$USER/
# or on Windows: %LOCALAPPDATA%\InstrumentServer\

# Remove stale files if needed
rm /tmp/instrument-server-$USER/server.pid
```

**Problem**: `Another server instance is already running`

```bash
# Check if daemon is actually running
instrument-server daemon status

# If status shows not running but error persists, remove PID file
rm /tmp/instrument-server-$USER/server.pid

# Or force stop
instrument-server daemon stop
```

### Instrument Issues

**Problem**:  Instrument won't start

```bash
# Check logs
tail -f instrument_server.log

# Check plugin availability
instrument-server plugins

# Test with specific plugin
instrument-server test config.yaml IDN --plugin ./custom. so

# Enable debug logging
instrument-server start config.yaml --log-level debug
```

**Problem**: `Instrument not found` when running measure

```bash
# Check which instruments are running
instrument-server list

# Check specific instrument status
instrument-server status DMM1

# Restart instrument if needed
instrument-server stop DMM1
instrument-server start configs/dmm1.yaml
```

### Plugin Issues

**Problem**: Plugin not found

```bash
# List available plugins
instrument-server plugins

# Discover in custom directories
instrument-server discover /usr/local/lib/instrument-plugins

# Check plugin file exists
ls -la /usr/local/lib/instrument-plugins/

# Use custom plugin explicitly
instrument-server start config.yaml --plugin /path/to/plugin.so
```

**Problem**: Plugin fails to load

```bash
# Check plugin dependencies (Linux)
ldd /path/to/plugin.so

# Check for undefined symbols
nm -D /path/to/plugin.so | grep " U "

# Enable debug logging
instrument-server start config.yaml --plugin ./plugin.so --log-level debug
```

### IPC Issues

**Problem**: Commands timeout

Check worker logs:

```bash
tail -f worker_DMM1.log
```

Increase timeout in script:

```lua
context.timeout = 10000  -- 10 seconds
```

**Problem**: Shared memory errors (Linux)

```bash
# Clean up stale shared memory segments
ipcs -m | grep $USER
ipcrm -m <shmid>

# Or restart daemon
instrument-server daemon stop
instrument-server daemon start
```

### Measurement Script Issues

**Problem**: Script errors

```bash
# Check Lua syntax
lua -l my_script.lua

# Enable debug logging
instrument-server measure dc my_script.lua --log-level debug

# Check instrument names match
instrument-server list
```

**Problem**: Parallel blocks don't synchronize

Ensure all instruments in parallel block are running:

```lua
context:parallel(function()
    -- Make sure these instruments exist
    context:call("DAC1.Set", 5.0)  -- DAC1 must be running
    context:call("DAC2.Set", 3.0)  -- DAC2 must be running
end)
```

### Performance Issues

**Problem**: Slow command execution

```bash
# Run performance tests
./build/tests/perf_tests

# Check IPC queue performance
# Check network latency (for VISA instruments)
ping 192.168.1.100

# Enable only error logging to reduce overhead
instrument-server measure dc script.lua --log-level error
```

### Getting Help

1. Check logs:
   - Main log: `instrument_server.log`
   - Worker logs: `worker_<instrument_name>.log`

2. Enable debug logging:

   ```bash
   instrument-server <command> --log-level debug
   ```

3. Run with test command:

   ```bash
   instrument-server test config.yaml COMMAND
   ```

4. Check daemon status:

   ```bash
   instrument-server daemon status
   ```

## Documentation

- [Architecture Overview](docs/ARCHITECTURE.md)
- [CLI Usage Guide](docs/CLI_USAGE.md)
- [Plugin Development](docs/PLUGIN_DEVELOPMENT.md)
- [Synchronization Protocol](docs/SYNCHRONIZATION.md)

## License

This project is licensed under the Mozilla Public License 2.0 - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please ensure:

1. All tests pass:  `make test`
2. Code follows existing style
3. New features include tests
4. Documentation is updated

## Authors

Falcon Autotuning Team
