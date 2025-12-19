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

<!--toc:start-->
- [Falcon Instrument Script Server](#falcon-instrument-script-server)
  - [Overview](#overview)
  - [Table of Contents](#table-of-contents)
  - [Quick Start](#quick-start)
  - [Architecture](#architecture)
    - [Key Components](#key-components)
  - [Quick Start:  Acquiring Waveform Data](#quick-start-acquiring-waveform-data)
  - [Installation](#installation)
    - [Dependencies](#dependencies)
    - [Ubuntu/Debian](#ubuntudebian)
    - [Build](#build)
    - [Verify Installation](#verify-installation)
  - [Usage](#usage)
    - [Starting the Daemon](#starting-the-daemon)
    - [Managing Instruments](#managing-instruments)
      - [Start Instruments](#start-instruments)
      - [List Running Instruments](#list-running-instruments)
      - [Check Instrument Status](#check-instrument-status)
      - [Stop Instrument](#stop-instrument)
    - [Running Measurements](#running-measurements)
    - [Testing Single Commands](#testing-single-commands)
    - [Plugin Discovery](#plugin-discovery)
  - [Built-in Plugins](#built-in-plugins)
    - [VISA Plugin (Built-in)](#visa-plugin-built-in)
      - [Prerequisites](#prerequisites)
      - [Configuration](#configuration)
      - [Initialization Commands](#initialization-commands)
      - [Command Templates](#command-templates)
      - [Usage Example](#usage-example)
      - [Verifying VISA Plugin](#verifying-visa-plugin)
    - [Creating Custom Plugins](#creating-custom-plugins)
  - [Running Measurements](#running-measurements)
    - [Example:  Simple IV Curve](#example-simple-iv-curve)
    - [Example: Parallel Execution](#example-parallel-execution)
    - [Example: Complex Measurement Loop](#example-complex-measurement-loop)
    - [RuntimeContext API](#runtimecontext-api)
      - [context:call()](#contextcall)
      - [context:parallel()](#contextparallel)
      - [context:log()](#contextlog)
  - [Higher-Level Measurement Frameworks](#higher-level-measurement-frameworks)
  - [Plugin Development](#plugin-development)
    - [Plugin Interface](#plugin-interface)
    - [Quick Plugin Creation](#quick-plugin-creation)
  - [Configuration](#configuration)
    - [Instrument Configuration](#instrument-configuration)
    - [API Definition](#api-definition)
  - [Testing](#testing)
    - [Running Tests](#running-tests)
    - [Test Organization](#test-organization)
    - [Writing Tests](#writing-tests)
  - [Troubleshooting](#troubleshooting)
    - [Daemon Issues](#daemon-issues)
    - [Instrument Issues](#instrument-issues)
    - [Plugin Issues](#plugin-issues)
    - [IPC Issues](#ipc-issues)
    - [Measurement Script Issues](#measurement-script-issues)
    - [Performance Issues](#performance-issues)
    - [Getting Help](#getting-help)
  - [Documentation](#documentation)
  - [License](#license)
  - [Contributing](#contributing)
  - [Authors](#authors)
<!--toc:end-->

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

## Quick Start:  Acquiring Waveform Data

Here's a complete example showing how to acquire and export waveform data:

```lua
-- waveform_capture.lua

-- Configure oscilloscope
context:call("Scope1.SET_TIMEBASE", {scale = 1e-6})  -- 1 µs/div
context:call("Scope1.SET_VOLTAGE", {channel = 1, scale = 1.0})  -- 1 V/div

-- Trigger single acquisition
context:call("Scope1.SINGLE")

-- Wait for completion
repeat
  local status = context:call("Scope1.GET_STATUS")
  context:sleep(50)
until status == "COMPLETE"

-- Acquire waveform (returns buffer for large data)
local result = context:call("Scope1.GET_WAVEFORM", {channel = 1})

if result.has_large_data then
  local buffer = get_buffer(result.buffer_id)
  
  print(string.format("Captured %d points", buffer.element_count))
  
  -- Export to CSV for analysis
  buffer:export_csv("waveform_ch1.csv")
  print("Exported to waveform_ch1.csv")
  
  -- Also save binary version (faster, more compact)
  buffer:export_binary("waveform_ch1.bin")
  
  buffer:release()
else
  print("Single value:  " .. result.value)
end
```

Run it:

```bash
instrument-server measure dc waveform_capture.lua
```

This will create:

- `waveform_ch1.csv` - Text format for spreadsheets/analysis
- `waveform_ch1.bin` - Binary format for fast loading in Python/MATLAB

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

## Built-in Plugins

The Instrument Script Server comes with pre-installed plugins for common instrument protocols.

### VISA Plugin (Built-in)

The **NI-VISA plugin** is automatically loaded and available for all instruments that use the VISA protocol.  This includes instruments connected via:

- **GPIB** (IEEE-488)
- **USB** (USB-TMC)
- **Ethernet/LAN** (VXI-11, LXI)
- **Serial** (RS-232/RS-485 over VISA)

#### Prerequisites

The VISA plugin requires **NI-VISA** to be installed on your system:

- **Linux**: Install `ni-visa` from National Instruments
- **Windows**: Download from [NI-VISA Downloads](https://www.ni.com/en-us/support/downloads/drivers/download.ni-visa.html)
- **macOS**: Download from [NI-VISA Downloads](https://www.ni.com/en-us/support/downloads/drivers/download.ni-visa.html)

#### Configuration

VISA instruments are configured using standard VISA resource strings:

```yaml
name: MyInstrument
api_ref: apis/my_instrument.yaml
connection: 
  type: VISA
  address: "TCPIP:: 192.168.1.100:: INSTR"  # Ethernet
  # address: "GPIB0::15::INSTR"           # GPIB
  # address: "USB0::0x1234::0x5678::SN123:: INSTR"  # USB
  # address:  "ASRL1::INSTR"               # Serial
  timeout: 5000                            # Timeout in milliseconds
  termination: "\\n"                       # Line termination (default:  \n)
```

#### Initialization Commands

You can specify initialization commands in your instrument API definition that will be sent when the instrument is first connected:

```yaml
initialization:
  - "*CLS"          # Clear status
  - "*RST"          # Reset instrument
  - "*OPC?"         # Wait for operations to complete

commands:
  # ...  your commands
```

#### Command Templates

The VISA plugin uses the `template` field in your API definition to construct commands.  Parameters in curly braces `{param}` are automatically substituted:

```yaml
commands:
  SET_VOLTAGE:
    description: "Set output voltage"
    template: ": SOUR: VOLT {voltage}"
    inputs:
      - voltage
    outputs:  []
    
  MEASURE_CURRENT:
    description: "Measure DC current"
    template: ": MEAS:CURR: DC?"
    inputs:  []
    outputs: 
      - current

io: 
  - name: voltage
    type: float
    unit: V
    
  - name: current
    type: float
    unit: A
```

#### Usage Example

```lua
-- Connect to VISA instrument
context: call("MyInstrument.SET_VOLTAGE", {voltage = 5.0})

-- Query with response
local current = context:call("MyInstrument.MEASURE_CURRENT")
print("Measured current:  " .. current ..  " A")
```

#### Verifying VISA Plugin

Check that the VISA plugin is loaded:

```bash
instrument-server plugins
```

Output should include:

```
Available plugins: 

  VISA -> /usr/local/lib/instrument-plugins/visa_plugin.so
  ... 
```

### Creating Custom Plugins

If you need to support protocols beyond VISA, see [Plugin Development Guide](docs/PLUGIN_DEVELOPMENT.md) for creating custom plugins.

## Running Measurements

Measurements are executed via Lua scripts. The script has access to a `context` object with three key functions:

- `context: call(command)` - Execute instrument command
- `context:parallel(function)` - Execute commands in synchronized parallel
- `context:log(message)` - Log messages

```bash
# Run any Lua measurement script
instrument-server measure my_measurement.lua
```

### Example:  Simple IV Curve

```lua
-- iv_curve.lua
context:log("Starting IV curve measurement")

-- Sweep voltage and measure current
for voltage = 0, 5, 0.1 do
    -- Set DAC voltage
    context:call("DAC1.SetVoltage", voltage)
    
    -- Wait for settling
    os.execute("sleep 0.01")
    
    -- Measure current
    local current = context: call("DMM1.MeasureCurrent")
    
    -- Output data
    print(string.format("%. 3f,%.6e", voltage, current))
    
    context:log(string.format("V=%.3f, I=%.6e", voltage, current))
end

context:log("IV curve complete")
```

Run it:

```bash
instrument-server measure iv_curve.lua > iv_data.csv
```

### Example: Parallel Execution

```lua
-- parallel_measurement.lua
context:log("Setting up measurement")

-- Set multiple instruments simultaneously
context:parallel(function()
    context:call("DAC1.SetVoltage", 1.5)
    context:call("DAC2.SetVoltage", 2.0)
    context:call("DAC3.SetVoltage", 0.5)
end)

context:log("All voltages set simultaneously")

-- Perform synchronized measurement
local results = {}
context:parallel(function()
    results. dmm1 = context:call("DMM1.Measure")
    results.dmm2 = context:call("DMM2.Measure")
end)

context:log(string.format("Results: DMM1=%.6f, DMM2=%.6f", 
                          results.dmm1, results. dmm2))
```

### Example: Complex Measurement Loop

```lua
-- stability_diagram.lua
context:log("Starting 2D stability diagram")

-- Sweep parameters
local v_gate_min, v_gate_max, v_gate_steps = -1.0, 1.0, 50
local v_bias_min, v_bias_max, v_bias_steps = -0.5, 0.5, 50

local v_gate_step = (v_gate_max - v_gate_min) / v_gate_steps
local v_bias_step = (v_bias_max - v_bias_min) / v_bias_steps

-- Sweep
for i_gate = 0, v_gate_steps do
    local v_gate = v_gate_min + i_gate * v_gate_step
    
    -- Set gate voltage
    context:call("DAC_Gate.SetVoltage", v_gate)
    os.execute("sleep 0.01")
    
    for i_bias = 0, v_bias_steps do
        local v_bias = v_bias_min + i_bias * v_bias_step
        
        -- Set bias and measure simultaneously
        context:parallel(function()
            context:call("DAC_Bias.SetVoltage", v_bias)
        end)
        
        -- Measure
        local current = context:call("DMM1.MeasureCurrent")
        
        -- Output:  gate_voltage, bias_voltage, current
        print(string.format("%. 6f,%.6f,%.6e", v_gate, v_bias, current))
    end
    
    if i_gate % 10 == 0 then
        context:log(string. format("Progress: %d/%d", i_gate, v_gate_steps))
    end
end

context:log("Stability diagram complete")
```

Run and save data:

```bash
instrument-server measure stability_diagram.lua > stability_data.csv
```

### RuntimeContext API

#### context:call()

Execute an instrument command:

```lua
-- Basic call
context:call("InstrumentName.CommandVerb")

-- With arguments (positional)
context:call("DAC1.SetVoltage", 5.0)

-- With channel
context:call("DAC1: 1.SetVoltage", 3.3)

-- With named parameters (table)
context:call("Scope1.Configure", {
    timebase = 1e-6,
    trigger_level = 0.5,
    channels = 2
})

-- Capture return value
local voltage = context:call("DMM1.MeasureVoltage")
local temperature = context:call("TempSensor.ReadTemperature")
```

#### context:parallel()

Execute commands in synchronized parallel:

```lua
context:parallel(function()
    -- All these execute simultaneously
    context:call("DAC1.Set", 1.0)
    context:call("DAC2.Set", 2.0)
    context:call("DAC3.Set", 3.0)
end)
-- Execution continues only after ALL complete
```

**Key behavior:**

- Commands inside `parallel()` buffer and execute simultaneously
- Block returns only when all instruments finish
- Instruments stay synchronized via barrier protocol
- Only works across different instruments (same instrument is sequential)

#### context:log()

Log messages during measurement:

```lua
context:log("Starting measurement")
context:log(string.format("Voltage: %.3f V", voltage))
```

Logs appear in `instrument_server.log` and stderr.

## Higher-Level Measurement Frameworks

The instrument server provides **generic primitives** (`call`, `parallel`, `log`). For specialized measurements like DC sweeps, waveform acquisition, or stability diagrams, you can:

1. **Write reusable Lua libraries**:

```lua
-- measurements/dc_sweep.lua
local dc_sweep = {}

function dc_sweep.run(setter, getter, v_min, v_max, v_steps)
    local step = (v_max - v_min) / v_steps
    local results = {}
    
    for i = 0, v_steps do
        local v = v_min + i * step
        context:call(setter ..  ".SetVoltage", v)
        os.execute("sleep 0.01")
        results[i+1] = context:call(getter .. ".Measure")
    end
    
    return results
end

return dc_sweep
```

Use it:

```lua
local dc_sweep = require("measurements.dc_sweep")
local data = dc_sweep.run("DAC1", "DMM1", 0, 5, 100)
```

2. **Build higher-level applications** that call `instrument-server measure` with generated scripts

3. **Create Python/Julia/etc. wrappers** that generate Lua scripts

This design keeps the core server simple and flexible!

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
