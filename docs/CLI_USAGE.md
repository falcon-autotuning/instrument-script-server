# Instrument Server CLI Usage Guide

Complete guide to using the `instrument-script-server` command-line interface.

## Table of Contents

<!--toc: start-->
- [Instrument Server CLI Usage Guide](#instrument-script-server-cli-usage-guide)
  - [Table of Contents](#table-of-contents)
  - [Overview](#overview)
    - [Command Categories](#command-categories)
    - [Global Options](#global-options)
  - [Daemon Management](#daemon-management)
    - [Start Daemon](#start-daemon)
    - [Stop Daemon](#stop-daemon)
    - [Check Daemon Status](#check-daemon-status)
  - [Instrument Management](#instrument-management)
    - [Start Instrument](#start-instrument)
    - [Stop Instrument](#stop-instrument)
    - [Check Instrument Status](#check-instrument-status)
    - [List All Instruments](#list-all-instruments)
  - [Measurements](#measurements)
    - [Measure Command](#measure-command)
    - [Script Structure](#script-structure)
    - [Command Format](#command-format)
    - [Example Scripts](#example-scripts)
    - [Building Measurement Libraries](#building-measurement-libraries)
    - [Integration with Higher-Level Software](#integration-with-higher-level-software)
  - [Plugin Management](#plugin-management)
    - [Discover Plugins](#discover-plugins)
  - [Logging](#logging)
    - [Log Levels](#log-levels)
    - [Log Files](#log-files)
    - [Viewing Logs](#viewing-logs)
  - [Complete Workflow Examples](#complete-workflow-examples)
    - [Example 1: Basic Measurement](#example-1-basic-measurement)
    - [Example 3: Multi-Instrument Setup](#example-3-multi-instrument-setup)
  - [Exit Codes](#exit-codes)
  - [Environment Variables](#environment-variables)
  - [See Also](#see-also)
<!--toc:end-->

## Overview

The `instrument-script-server` command provides a unified interface for all instrument server operations.
All commands follow the pattern:

```bash
instrument-script-server <command> [subcommand] [options]
```

### Command Categories

| Category | Commands | Description |
|----------|----------|-------------|
| **Daemon** | `daemon start/stop/status` | Manage server daemon |
| **Instruments** | `inst start/stop/status/list` | Manage instruments |
| **Measurements** | `measure <script>` | Run measurement scripts |
| **Jobs** | `job list/cancel/status/measure/result` | Queue background measurement jobs |
| **Buffer Management** | `buffer list/metadata/read/release` | Manage shared-memory buffers |
| **Utilities** | `discover [paths...]` | Discover plugins |

### Global Options

```bash
--help, -h            Show help message
--version, -v         Show the version
--json                Get a programmatic debug output
```

## Daemon Management

The server daemon is a background process that manages the instrument registry and coordinates all operations.

### Start Daemon

```bash
instrument-script-server daemon start [--log-level <level>  Set logging level (debug|info|warn|error)]
```

**Example:**

```bash
# Start with default logging (info)
instrument-script-server daemon start

# Start with debug logging
instrument-script-server daemon start --log-level debug
```

**Output:**

```
Daemon started 
```

**Notes:**

- Must be running before any instrument operations
- Only one daemon instance can run at a time
- Daemon persists until explicitly stopped
- PID file stored at`INSTRUMENT_SCRIPT_SERVER_RUNTIME_DIR`

### Stop Daemon

```bash
instrument-script-server daemon stop
```

**Example:**

```bash
instrument-script-server daemon stop
```

**Output:**

```
Daemon stopped
```

**Notes:**

- Stops all running instruments gracefully
- Cleans up IPC resources
- Removes PID file

### Check Daemon Status

```bash
instrument-script-server daemon status
```

**Example:**

```bash
instrument-script-server daemon status
```

**Output (if running):**

```
Server daemon is running (PID: 12345)
```

**Output (if not running):**

```
Daemon is not running
```

**Exit codes:**

- `0`: Daemon is running
- `1`: Daemon is not running

## Instrument Management

### Start Instrument

```bash
instrument-script-server inst start <config> [--plugin <path>] [--log-level <level>]
```

**Arguments:**

- `<config>`: Path to instrument configuration YAML file
- `--plugin <path>`: custom plugin (.so on Linux, .dll on Windows)
- `--log-level <level>`: Logging level (default: info)

**Examples:**

```bash
# Start instrument with discovered plugin
instrument-script-server inst start configs/dmm1.yaml

# Start with custom plugin
instrument-script-server inst start configs/custom_instrument.yaml --plugin ./my_plugin.so

# Start with debug logging
instrument-script-server inst start configs/dac1.yaml --log-level debug

# Start multiple instruments
instrument-script-server inst start configs/dac1.yaml
instrument-script-server inst start configs/dac2.yaml
instrument-script-server inst start configs/dmm1.yaml
```

**Output:**

```
Instrument started successfully
```

**Requirements:**

- Server daemon must be running
- Configuration file must exist and be valid
- Plugin for the protocol type must be available (unless --plugin specified)

### Stop Instrument

```bash
instrument-script-server inst stop <name>
```

**Arguments:**

- `<name>`: Instrument name (from config file)

**Example:**

```bash
instrument-script-server inst stop DMM1
```

**Output:**

```
Stopped instrument: DMM1
```

### Check Instrument Status

```bash
instrument-script-server inst status <name>
```

**Arguments:**

- `<name>`: Instrument name

**Example:**

```bash
instrument-script-server inst status DMM1
```

**Output:**

```
Instrument: DMM1
  Status: RUNNING
  Commands sent: 150
  Commands completed: 148
  Commands failed: 0
  Commands timeout: 2
```

**Status Fields:**

- **Status**: RUNNING or STOPPED
- **Commands sent**: Total commands dispatched
- **Commands completed**: Successfully executed commands
- **Commands failed**: Commands that returned errors
- **Commands timeout**: Commands that exceeded timeout

### List All Instruments

```bash
instrument-script-server inst list
```

**Example:**

```bash
instrument-script-server inst list
```

**Output:**

```
Running instruments:
  DAC1 [RUNNING]
  DAC2 [RUNNING]
  DMM1 [RUNNING]
  Scope1 [STOPPED]
```

**Notes:**

- Shows all instruments registered with the daemon
- `[RUNNING]`: Worker process is active
- `[STOPPED]`: Worker process has died or been stopped

## Measurements

Run Lua measurement scripts that control running instruments.

### Measure Command

```bash
instrument-script-server measure <script> [--global key=value] (repeatable) [--globals-json <json>]
```

**Arguments:**

- `<script>`: Path to Lua measurement script
- `--global key=value`: Optional key value pairs for simple primitives containing global variables for the measurement script.
  Keys are the names in the global namespace, Values are the values.
- `--globals-json <json>`: Optional JSON file containing global variables for the measurement script. This is useful for passing complex data structures (arrays, objects) to the script.

**Requirements:**

- Server daemon must be running
- Required instruments must be started
- Script file must exist and be valid Lua

**Examples:**

```bash
# Run measurement script with text output
instrument-script-server measure scripts/iv_curve.lua

# Get results in JSON format for programmatic parsing
instrument-script-server measure scripts/iv_curve.lua --json
```

#### Automatic Result Collection

All `ctx:call()` operations are automatically collected with full metadata, including:

- Command ID and execution timestamp
- Instrument name and verb (command)
- Parameters passed to the command
- Return value and type
- Large data buffer references (for waveforms, arrays, etc.)

Results are displayed after script execution in execution order, providing complete traceability of all measurements.

#### Text Output Format

By default, results are displayed in a human-readable format:

```
Measurement complete

  Status: (0-6) 0=UNSPECIFIED, 1=QUEUED, 2=RUNNING, 3=COMPLETED, 4=FAILED, 5=CANCELLING, 6=CANCELLED
  Command:
    Instrument: MockInstrument1
    Channel: 1
    Verb: Set
  Command:  
    Instrument: MockInstrument1
    Channel: 2
    Verb: Set 
  Command: 
    Instrument: MockInstrument1 
    Channel: 1
    Verb: GET 
      voltage = 5.0
  Command: 
    Instrument: MockInstrument1
    Channel: 2
    Verb: GET 
      voltage = 3.0
  Command: 
    Instrument: Scope1 
    Verb: CAPTURE 
      buffer = buf_abc123
```

Each line shows:

- **Instrument and Command**: Full command with channel and group if applicable
- **Return Value**: name followed by the value

Note that for data buffer for large quantities of data, the buffer ID is returned as the value.

#### JSON Output Format

Use `--json` flag to get structured output for automation and data processing:

```bash
instrument-script-server measure script.lua --json
```

**JSON Schema**: The output conforms to the JSON schema at `schemas/measurement_results.schema.json` for validation and automated parsing.

### Script Structure

All scripts have access to a global `ctx` object:

```lua
-- ctx:call(command, args...)     - Execute instrument command (returns MeasurementResponse)
-- ctx:parallel(function)         - Synchronized parallel execution
-- ctx:log(message)               - Log message

ctx:log("Script starting")

-- Your measurement logic here
local resp = ctx:call("DMM1.Measure")
local value = resp:value()  -- Extract actual value
print(value)

ctx:log("Script complete")
```

### MeasurementResponse Return Type

All `ctx:call()` operations return `MeasurementResponse` objects that wrap the measurement value with metadata:

```lua
local response = ctx:call("DMM.MEASURE")

-- Access metadata
print(response:instrument())  -- "DMM"
print(response:verb())        -- "MEASURE"
print(response:type())        -- "float"|"integer"|"string"|"boolean"|"buffer"

-- Extract the actual value
local value = response:value()

-- Perform math operations (returns new MeasurementResponse)
local adjusted = response:add_offset(-0.5)        -- Add offset
local scaled = adjusted:multiply_gain(2.0)        -- Multiply by gain
local final_value = scaled:value()                -- Extract result

-- For arrays/buffers:
local array_resp = ctx:call("Scope.GET_WAVEFORM")
local buffer = array_resp:buffer()  -- Get BufferHandle
buffer:add_offset(-0.5)             -- Offset all elements
buffer:multiply_gain(10.0)          -- Gain all elements
```

**MeasurementResponse Methods:**

- `instrument()` → string - Returns instrument name
- `verb()` → string - Returns command/verb name
- `type()` → string - Returns value type
- `value()` → any - Returns actual value (number, string, boolean, or BufferHandle)
- `add_offset(offset)` → MeasurementResponse - Adds offset to numeric values
- `multiply_gain(gain)` → MeasurementResponse - Multiplies numeric values by gain
- `buffer()` → BufferHandle - For array types, returns buffer handle

**BufferHandle Methods (for arrays):**

- `id()` → string - Buffer ID
- `size()` → integer - Number of elements
- `type()` → string - Data type
- `add_offset(offset)` → boolean - Apply offset to all elements
- `multiply_gain(gain)` → boolean - Apply gain to all elements

### Command Format

```lua
-- Basic:  InstrumentName.CommandVerb
ctx:call("DAC1.SetVoltage", 5.0)

-- With channel:  InstrumentName:Channel.CommandVerb
ctx:call("DAC1:1.SetVoltage", 3.3)

-- Return value extraction
local voltage_resp = ctx:call("DMM1.MeasureVoltage")
local voltage = voltage_resp:value()
```

### Example Scripts

**Simple sweep with value extraction:**

```lua
for v = 0, 5, 0.1 do
    ctx:call("DAC1.Set", v)
    local i_resp = ctx:call("DMM1.Measure")
    local i = i_resp:value()
    print(string.format("%.3f,%.6e", v, i))
end
```

**Using built-in math operations:**

```lua
for v = 0, 5, 0.1 do
    ctx:call("DAC1.Set", v)
    local i_resp = ctx:call("DMM1.Measure")
    -- Apply offset and gain corrections
    local corrected = i_resp:add_offset(-0.001):multiply_gain(1.05)
    print(string.format("%.3f,%.6e", v, corrected:value()))
end
```

**Parallel execution:**

```lua
ctx:parallel(function()
    ctx:call("DAC1.Set", 1.0)
    ctx:call("DAC2.Set", 2.0)
end)
-- Both DACs set simultaneously
```

**2D measurement:**

```lua
for x = 0, 10 do
    ctx:call("DAC_X.Set", x * 0.1)
    
    for y = 0, 10 do
        ctx:parallel(function()
            ctx:call("DAC_Y.Set", y * 0.05)
        end)
        
        local z_resp = ctx:call("DMM1.Measure")
        local z = z_resp:value()
        print(string.format("%d,%d,%.6e", x, y, z))
    end
end
```

### Building Measurement Libraries

Create reusable Lua modules for common measurement patterns:

```bash
# Directory structure
measurements/
├── dc_sweep.lua
├── waveform_1d.lua
└── stability_diagram.lua
```

```lua
-- measurements/dc_sweep.lua
local M = {}

function M.sweep(setter, getter, v_start, v_stop, v_step)
    local data = {}
    local v = v_start
    
    while v <= v_stop do
        ctx:call(setter, v)
        os.execute("sleep 0.01")
        local measured_resp = ctx:call(getter)
        local measured = measured_resp:value()
        table.insert(data, {v, measured})
        v = v + v_step
    end
    
    return data
end

return M
```

Use it:

```lua
package.path = package.path .. ";./measurements/?. lua"
local dc_sweep = require("dc_sweep")

local results = dc_sweep.sweep("DAC1.SetVoltage", "DMM1.Measure", 0, 5, 0.1)

for _, point in ipairs(results) do
    print(string.format("%. 3f,%.6e", point[1], point[2]))
end
```

### Integration with Higher-Level Software

The generic `measure` command allows integration with any high-level software:

**Python example:**

```python
import subprocess
import tempfile

# Generate Lua script
lua_script = """
ctx:log("Starting measurement")
for v = 0, 5, 0.1 do
    ctx:call("DAC1.Set", v)
    local i = ctx:call("DMM1.Measure")
    print(string.format("%.3f,%.6e", v, i))
end
"""

# Write to temp file
with tempfile.NamedTemporaryFile(mode='w', suffix='.lua', delete=False) as f:
    f.write(lua_script)
    script_path = f.name

# Run measurement
result = subprocess.run(
    ['instrument-script-server', 'measure', script_path],
    capture_output=True,
    text=True
)

# Parse results
for line in result.stdout.split('\n'):
    if line.strip():
        voltage, current = map(float, line.split(','))
        print(f"V={voltage}V, I={current}A")
```

This architecture keeps the instrument server simple and generic, while allowing arbitrarily complex measurement logic in higher-level frameworks!

## Buffer Management

Large measurement arrays (e.g., waveforms, multi-channel datasets) are stored in shared memory to achieve high throughput and zero copies. You can manage these buffers directly from the CLI.

### 1. `buffer list`

Lists all active shared memory buffer IDs currently allocated, along with their metadata.

```bash
instrument-script-server buffer list
```

**Example Output:**

```
Active buffers:
  buffer_1779829276760326 (10000 elements, type=float32)
  buffer_1779829276760734 (10000 elements, type=float32)
```

### 2. `buffer metadata`

Displays complete structural metadata for a specific buffer ID.

```bash
instrument-script-server buffer metadata <buffer_id>
```

**Example:**

```bash
instrument-script-server buffer metadata buffer_1779829276760326
```

**Output:**

```
Buffer Metadata:
  ID: buffer_1779829276760326
  Elements: 10000
  Type: float32
  Size: 40000 bytes
```

### 3. `buffer read`

Reads and displays the data contents of a shared memory buffer.

```bash
instrument-script-server buffer read <buffer_id> [--json]
```

**Arguments:**

- `<buffer_id>`: Target buffer identifier
- `--json`: Optional. Formats data as a structured JSON payload containing the elements array.

**Example (Text format):**

```bash
instrument-script-server buffer read buffer_1779829276760326
```

**Output:**

```
[0] 0
[1] 0.0627905
[2] 0.125333
...
```

**Example (JSON format):**

```bash
instrument-script-server buffer read buffer_1779829276760326 --json
```

**Output:**

```json
{
  "ok": true,
  "buffer_id": "buffer_1779829276760326",
  "element_count": 10000,
  "data_type": "float64",
  "data": [0.0, 0.06279051952931337, 0.12533323356430426, ...]
}
```

### 4. `buffer release`

Decrements the server-side reference count and deallocates the shared-memory buffer if the reference count drops to 0.

```bash
instrument-script-server buffer release <buffer_id>
```

**Example:**

```bash
instrument-script-server buffer release buffer_1779829276760326
```

**Output:**

```
Released buffer: buffer_1779829276760326
```

> [!WARNING]
> Shared memory buffers persist in system memory until explicitly released. If you consume buffers in high-frequency automation loops, always call `buffer release` to prevent memory fragmentation and exhaustion.

## Plugin Management

Discover and manage instrument driver plugins.

### Discover Plugins

```bash
instrument-script-server discover [path1] [path2] ...
```

**Arguments:**

- `[paths]`: Optional directories to search (uses defaults if none provided)

**Example:**

```bash
# Discover in default locations
instrument-script-server discover

# Discover in custom directories
instrument-script-server discover /opt/custom-plugins ./local-plugins
```

**Output:**

```
Discovering plugins in: 
  /opt/custom-plugins
  ./local-plugins

Found 2 plugin(s):

Protocol:  CustomDAQ
  Path: /opt/custom-plugins/custom_daq.so
  Name: Custom DAQ Plugin
  Version: 2.1.0
  Description: High-speed data acquisition plugin

Protocol: MySerial
  Path: ./local-plugins/my_serial.so
  Name: My Serial Driver
  Version: 1.0.0
  Description: Custom serial protocol implementation
```

### Log Levels

| Level | Description | Use Case |
|-------|-------------|----------|
| `error` | Errors only | Production, minimal output |
| `warn` | Warnings and errors | Production |
| `info` | Informational messages | Normal operation (default) |
| `debug` | Detailed debugging | Development, troubleshooting |
| `trace` | Very detailed trace | Deep debugging |

### Log Files

**Main log:** `instrument_server.log`

- Contains server daemon and command logs
- Location: Current directory when command executed

**Worker logs:** `worker_<instrument_name>.log`

- One log per instrument worker process
- Contains plugin execution details
- Location: Current directory

**Example log locations:**

```
./instrument_server.log
./worker_DMM1.log
./worker_DAC1.log
./worker_Scope1.log
```

## Complete Workflow Examples

### Example 1: Basic Measurement

```bash
# 1. Start daemon
instrument-script-server daemon start

# 2. Start instruments
instrument-script-server inst start configs/dac1.yaml
instrument-script-server inst start configs/dmm1.yaml

# 3. Verify instruments are running
instrument-script-server inst list

# 4. Run measurement
instrument-script-server measure scripts/iv_curve.lua

# 5. Check instrument status
instrument-script-server inst status DMM1

# 6. Stop instruments
instrument-script-server inst stop DAC1
instrument-script-server inst stop DMM1

# 7. Stop daemon
instrument-script-server daemon stop
```

### Example 2: Multi-Instrument Setup

```bash
# 1. Start daemon
instrument-script-server daemon start

# 2. Discover available plugins
instrument-script-server discover

# 3. Start multiple instruments
instrument-script-server inst start configs/dac1.yaml
instrument-script-server inst start configs/dac2.yaml
instrument-script-server inst start configs/dac3.yaml
instrument-script-server inst start configs/dmm1.yaml
instrument-script-server inst start configs/dmm2.yaml
instrument-script-server inst start configs/scope1.yaml

# 4. Verify all running
instrument-script-server inst list

# 5. Check individual status
for inst in DAC1 DAC2 DAC3 DMM1 DMM2 Scope1; do
    echo "=== $inst ==="
    instrument-script-server inst status $inst
done

# 6. Run complex measurement with parallel execution
instrument-script-server measure scripts/stability_diagram.lua

# 7. Selective shutdown
instrument-script-server inst stop Scope1

# 8. Continue with remaining instruments
instrument-script-server measure scripts/final_measurement.lua

# 9. Complete shutdown
instrument-script-server daemon stop
```

## Exit Codes

All commands return exit codes for scripting:

| Code | Meaning |
|------|---------|
| `0` | Success |
| `1` | General error |

**Example script:**

```bash
#!/bin/bash

instrument-script-server daemon start
if [ $? -ne 0 ]; then
    echo "Failed to start daemon"
    exit 1
fi

instrument-script-server inst start configs/dmm1.yaml
if [ $? -ne 0 ]; then
    echo "Failed to start DMM1"
    instrument-script-server daemon stop
    exit 1
fi

instrument-script-server measure scripts/measurement.lua
result=$? 

instrument-script-server daemon stop
exit $result
```

## Environment Variables

The server supports configuration via environment variables:

### Measurement Timeout

- **Variable**: `MEASUREMENT_TIMEOUT_SEC`
- **Default**: `5`
- **Description**: Sets the longest possible safe measurement time. After this maximal time the server will abort the measurement.

### RPC Port Configuration

- **Variable**: `INSTRUMENT_SCRIPT_SERVER_RPC_PORT`
- **Default**: `8555`
- **Description**: Sets the HTTP RPC server port on localhost for API access

### Max Job History

- **Variable**: `INSTRUMENT_SCRIPT_SERVER_MAX_JOB_HISTORY`
- **Default**: `10000`
- **Description**: Sets the maximum number of completed jobs to keep in memory. Older jobs will be discarded.

### External Lua Measurement Library Path

- **Variable**: `INSTRUMENT_SCRIPT_SERVER_OPT_LUA_LIB`
- **Default**: ``
- **Description**: Sets the path(s) for optional Lua libraries to load for interpreting measurement scripts. Supports:
  - A single directory containing Lua modules
  - A single Lua bundle file
  - Multiple paths separated by semicolons (`;`)
  - Example: `export INSTRUMENT_SCRIPT_SERVER_OPT_LUA_LIB="/path/to/lib1;/path/to/lib2;/path/to/bundle.lua"`

### Runtime Dir

- **Variable**: `INSTRUMENT_SCRIPT_SERVER_RUNTIME_DIR`
- **Default**: windows: `LOCALAPPDATA`; linux: `XDG_RUNTIME_DIR`
- **Description**: Sets the path for runtime pid file for the Server Daemon

## See Also

- [Main README](../README.md) - Getting started and overview
- [Configuration Guide](CONFIGURATION.md) - Writing configuration files
- [Plugin Development](PLUGIN_DEVELOPMENT.md) - Writing instrument plugins
