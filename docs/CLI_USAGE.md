# Instrument Server CLI Usage Guide

Complete guide to using the `instrument-server` command-line interface.

## Table of Contents

<!--toc: start-->
- [Instrument Server CLI Usage Guide](#instrument-server-cli-usage-guide)
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
  - [Testing](#testing)
    - [Test Command](#test-command)
  - [Plugin Management](#plugin-management)
    - [List Available Plugins](#list-available-plugins)
    - [Discover Plugins](#discover-plugins)
  - [Configuration Validation](#configuration-validation)
    - [Validate Command](#validate-command)
  - [Logging](#logging)
    - [Log Levels](#log-levels)
    - [Log Files](#log-files)
    - [Viewing Logs](#viewing-logs)
  - [Complete Workflow Examples](#complete-workflow-examples)
    - [Example 1: Basic Measurement](#example-1-basic-measurement)
    - [Example 2: Development Workflow](#example-2-development-workflow)
    - [Example 3: Multi-Instrument Setup](#example-3-multi-instrument-setup)
    - [Example 4: Troubleshooting](#example-4-troubleshooting)
  - [Exit Codes](#exit-codes)
  - [Environment Variables](#environment-variables)
  - [See Also](#see-also)
<!--toc:end-->

## Overview

The `instrument-server` command provides a unified interface for all instrument server operations.
All commands follow the pattern:

```bash
instrument-server <command> [subcommand] [options]
```

### Command Categories

| Category | Commands | Description |
|----------|----------|-------------|
| **Daemon** | `daemon start/stop/status` | Manage server daemon |
| **Instruments** | `start`, `stop`, `status`, `list` | Manage instruments |
| **Measurements** | `measure <script>` | Run measurement scripts |
| **Testing** | `test <config> <verb>` | Test instrument commands |
| **Plugins** | `plugins`, `discover` | Manage plugins |
| **Validation** | `validate config/api <file>` | Validate configuration files |

### Global Options

```bash
--log-level <level>   Set logging level (debug|info|warn|error)
--help, -h            Show help message
```

## Daemon Management

The server daemon is a background process that manages the instrument registry and coordinates all operations.

### Start Daemon

```bash
instrument-server daemon start [--log-level <level>]
```

**Example:**

```bash
# Start with default logging (info)
instrument-server daemon start

# Start with debug logging
instrument-server daemon start --log-level debug
```

**Output:**

```
Server daemon started (PID: 12345)
Daemon running in background
```

**Notes:**

- Must be running before any instrument operations
- Only one daemon instance can run at a time
- Daemon persists until explicitly stopped
- On Linux:  PID file stored in `/tmp/instrument-server-$USER/server. pid`
- On Windows: PID file stored in `%LOCALAPPDATA%\InstrumentServer\server.pid`

### Stop Daemon

```bash
instrument-server daemon stop
```

**Example:**

```bash
instrument-server daemon stop
```

**Output:**

```
Stopping server daemon (PID: 12345)...
Server daemon stopped
```

**Notes:**

- Stops all running instruments gracefully
- Cleans up IPC resources
- Removes PID file

### Check Daemon Status

```bash
instrument-server daemon status
```

**Example:**

```bash
instrument-server daemon status
```

**Output (if running):**

```
Server daemon is running (PID: 12345)
Runtime directory: /tmp/instrument-server-user/server. pid
```

**Output (if not running):**

```
Server daemon is not running
```

**Exit codes:**

- `0`: Daemon is running
- `1`: Daemon is not running

## Instrument Management

### Start Instrument

```bash
instrument-server start <config> [--plugin <path>] [--log-level <level>]
```

**Arguments:**

- `<config>`: Path to instrument configuration YAML file
- `--plugin <path>`: Optional custom plugin (. so on Linux, .dll on Windows)
- `--log-level <level>`: Logging level (default: info)

**Examples:**

```bash
# Start instrument with discovered plugin
instrument-server start configs/dmm1.yaml

# Start with custom plugin
instrument-server start configs/custom_instrument.yaml --plugin ./my_plugin.so

# Start with debug logging
instrument-server start configs/dac1.yaml --log-level debug

# Start multiple instruments
instrument-server start configs/dac1.yaml
instrument-server start configs/dac2.yaml
instrument-server start configs/dmm1.yaml
```

**Output:**

```
Started instrument:  DMM1
```

**Requirements:**

- Server daemon must be running
- Configuration file must exist and be valid
- Plugin for the protocol type must be available (unless --plugin specified)

### Stop Instrument

```bash
instrument-server stop <name>
```

**Arguments:**

- `<name>`: Instrument name (from config file)

**Example:**

```bash
instrument-server stop DMM1
```

**Output:**

```
Stopped instrument: DMM1
```

### Check Instrument Status

```bash
instrument-server status <name>
```

**Arguments:**

- `<name>`: Instrument name

**Example:**

```bash
instrument-server status DMM1
```

**Output:**

```
Instrument:  DMM1
  Status:  RUNNING
  Commands sent: 150
  Commands completed: 148
  Commands failed: 0
  Commands timeout: 2
```

**Status Fields:**

- **Status**:  RUNNING or STOPPED
- **Commands sent**:  Total commands dispatched
- **Commands completed**: Successfully executed commands
- **Commands failed**: Commands that returned errors
- **Commands timeout**: Commands that exceeded timeout

### List All Instruments

```bash
instrument-server list
```

**Example:**

```bash
instrument-server list
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
instrument-server measure <script> [--json] [--log-level <level>]
```

**Arguments:**

- `<script>`: Path to Lua measurement script
- `--json`: Output results in JSON format (default: text format)
- `--log-level <level>`: Logging level (default: info)

**Requirements:**

- Server daemon must be running
- Required instruments must be started
- Script file must exist and be valid Lua

**Examples:**

```bash
# Run measurement script with text output
instrument-server measure scripts/iv_curve.lua

# Get results in JSON format for programmatic parsing
instrument-server measure scripts/iv_curve.lua --json

# With debug logging
instrument-server measure scripts/test.lua --log-level debug

# Save JSON output to file
instrument-server measure scripts/sweep.lua --json > results.json
```

#### Automatic Result Collection

All `context:call()` operations are automatically collected with full metadata, including:

- Command ID and execution timestamp
- Instrument name and verb (command)
- Parameters passed to the command
- Return value and type
- Large data buffer references (for waveforms, arrays, etc.)

Results are displayed after script execution in execution order, providing complete traceability of all measurements.

#### Text Output Format

By default, results are displayed in a human-readable format:

```
Running measurement...
Measurement complete

=== Script Results ===
[0] MockInstrument1:1.SET(5.0) -> [bool] true
[1] MockInstrument1:2.SET(3.0) -> [bool] true
[2] MockInstrument1:1.GET() -> [double] 5.0
[3] MockInstrument1:2.GET() -> [double] 3.0
[4] Scope1.CAPTURE() -> [buffer] buf_abc123 (10000 elements, float32)
======================
```

Each line shows:

- **Index**: Sequential number of the call
- **Instrument and Command**: Full command with channel if applicable
- **Parameters**: Values passed to the command
- **Return Value**: Type in brackets, followed by the value

For large data buffers (waveforms, large arrays), the output shows a reference with:

- **buffer_id**: Unique identifier for accessing the data
- **element_count**: Number of data points
- **data_type**: Type of data (float32, float64, int32, etc.)

#### JSON Output Format

Use `--json` flag to get structured output for automation and data processing:

```bash
instrument-server measure script.lua --json
```

Output structure:

```json
{
  "status": "success",
  "script": "iv_curve.lua",
  "results": [
    {
      "index": 0,
      "instrument": "MockInstrument1:1",
      "verb": "SET",
      "params": {"value": 5.0},
      "executed_at_ms": 1704720615123,
      "return": {
        "type": "bool",
        "value": true
      }
    },
    {
      "index": 4,
      "instrument": "Scope1",
      "verb": "CAPTURE",
      "params": {},
      "executed_at_ms": 1704720615127,
      "return": {
        "type": "buffer",
        "buffer_id": "buf_abc123",
        "element_count": 10000,
        "data_type": "float32"
      }
    }
  ]
}
```

**JSON Schema**: The output conforms to the JSON schema at `schemas/measurement_results.schema.json` for validation and automated parsing.

**Return Types**:

- `double`: Floating-point number
- `int64`: Integer value
- `string`: Text value
- `bool`: Boolean (true/false)
- `array`: Array of numbers
- `buffer`: Reference to large data buffer
- `void`: Command with no return value

### Script Structure

All scripts have access to a global `context` object:

```lua
-- context: call(command, args...)     - Execute instrument command
-- context:parallel(function)         - Synchronized parallel execution
-- context:log(message)               - Log message

context:log("Script starting")

-- Your measurement logic here
local value = context:call("DMM1.Measure")
print(value)

context:log("Script complete")
```

### Command Format

```lua
-- Basic:  InstrumentName.CommandVerb
context:call("DAC1.SetVoltage", 5.0)

-- With channel:  InstrumentName: Channel.CommandVerb
context:call("DAC1:1.SetVoltage", 3.3)

-- Return value
local voltage = context:call("DMM1.MeasureVoltage")
```

### Example Scripts

**Simple sweep:**

```lua
for v = 0, 5, 0.1 do
    context:call("DAC1.Set", v)
    local i = context:call("DMM1.Measure")
    print(string.format("%. 3f,%. 6e", v, i))
end
```

**Parallel execution:**

```lua
context:parallel(function()
    context:call("DAC1.Set", 1.0)
    context:call("DAC2.Set", 2.0)
end)
-- Both DACs set simultaneously
```

**2D measurement:**

```lua
for x = 0, 10 do
    context:call("DAC_X.Set", x * 0.1)
    
    for y = 0, 10 do
        context:parallel(function()
            context:call("DAC_Y.Set", y * 0.05)
        end)
        
        local z = context:call("DMM1.Measure")
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
        context:call(setter, v)
        os.execute("sleep 0.01")
        local measured = context:call(getter)
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
context:log("Starting measurement")
for v = 0, 5, 0.1 do
    context:call("DAC1.Set", v)
    local i = context:call("DMM1.Measure")
    print(string.format("%.3f,%.6e", v, i))
end
"""

# Write to temp file
with tempfile.NamedTemporaryFile(mode='w', suffix='.lua', delete=False) as f:
    f.write(lua_script)
    script_path = f.name

# Run measurement
result = subprocess.run(
    ['instrument-server', 'measure', script_path],
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

## Testing

Test individual instrument commands without writing full scripts.

### Test Command

```bash
instrument-server test <config> <verb> [param=value ... ] [--plugin <path>] [--log-level <level>]
```

**Arguments:**

- `<config>`: Path to instrument configuration file
- `<verb>`: Command verb from API definition
- `param=value`: Command parameters (key=value pairs)
- `--plugin <path>`: Optional custom plugin
- `--log-level <level>`: Logging level

**Examples:**

```bash
# Test identity query
instrument-server test configs/dmm1.yaml IDN

# Test with parameters
instrument-server test configs/dac1.yaml SET_VOLTAGE channel=1 voltage=5.0

# Test with custom plugin
instrument-server test configs/custom. yaml MEASURE --plugin ./my_plugin.so

# Test with debug logging
instrument-server test configs/scope1.yaml TRIGGER --log-level debug
```

**Output:**

```
Testing instrument: DMM1
Executing command: IDN

Result: 
  Success: YES
  Response:  Keithley Instruments Inc., Model 2400, 1234567, v1.0
```

**Notes:**

- Creates temporary instrument instance for testing
- Instrument is automatically stopped after test
- Useful for verifying plugin functionality
- Does not require daemon (starts temporary instance)

## Plugin Management

Discover and manage instrument driver plugins.

### List Available Plugins

```bash
instrument-server plugins
```

**Example:**

```bash
instrument-server plugins
```

**Output:**

```
Available plugins:

  VISA -> /usr/local/lib/instrument-plugins/visa_builtin.so
  SimpleSerial -> /usr/local/lib/instrument-plugins/simple_serial_plugin.so
  MockTest -> ./mock_plugin.so

Total:  3 plugin(s)
```

**Notes:**

- Searches standard directories:
  - `/usr/local/lib/instrument-plugins/`
  - `/usr/lib/instrument-plugins/`
  - `./plugins/`
  - `.` (current directory)

### Discover Plugins

```bash
instrument-server discover [path1] [path2] ...
```

**Arguments:**

- `[paths]`: Optional directories to search (uses defaults if none provided)

**Example:**

```bash
# Discover in default locations
instrument-server discover

# Discover in custom directories
instrument-server discover /opt/custom-plugins ./local-plugins
```

**Output:**

```
Discovering plugins in: 
  /opt/custom-plugins
  ./local-plugins

Found 2 plugin(s):

Protocol:  CustomDAQ
  Path: /opt/custom-plugins/custom_daq. so
  Name: Custom DAQ Plugin
  Version: 2.1.0
  Description: High-speed data acquisition plugin

Protocol: MySerial
  Path: ./local-plugins/my_serial.so
  Name: My Serial Driver
  Version: 1.0.0
  Description: Custom serial protocol implementation
```

## Configuration Validation

Validate configuration files against JSON schemas before using them.

### Validate Command

```bash
# Validate instrument configuration
instrument-server validate config <file>

# Validate API definition
instrument-server validate api <file>
```

**Examples:**

```bash
# Validate instrument configuration
instrument-server validate config examples/instrument-configurations/agi_34401_config.yaml

# Validate API definition
instrument-server validate api examples/instrument-apis/agi_34401a.yaml
```

**Output (success):**

```
✓ Configuration is valid
```

**Output (error):**

```
✗ Validation failed:
  - Field 'name' must match pattern ^[A-Z][A-Z0-9_]*$
  - Missing required field 'io_config'
```

**Notes:**

- Validates against JSON schemas in `schemas/` directory
- Checks required fields, data types, and constraints
- Use before starting instruments to catch configuration errors early

## Logging

All commands support logging configuration via `--log-level`.

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

### Viewing Logs

```bash
# View main log
tail -f instrument_server.log

# View specific worker log
tail -f worker_DMM1.log

# Search for errors
grep ERROR *.log

# Search for specific instrument
grep "DMM1" instrument_server.log
```

## Complete Workflow Examples

### Example 1: Basic Measurement

```bash
# 1. Start daemon
instrument-server daemon start

# 2. Start instruments
instrument-server start configs/dac1.yaml
instrument-server start configs/dmm1.yaml

# 3.  Verify instruments are running
instrument-server list

# 4. Run measurement
instrument-server measure scripts/iv_curve.lua

# 5. Check instrument status
instrument-server status DMM1

# 6. Stop instruments
instrument-server stop DAC1
instrument-server stop DMM1

# 7. Stop daemon
instrument-server daemon stop
```

### Example 2: Development Workflow

```bash
# 1. Start daemon with debug logging
instrument-server daemon start --log-level debug

# 2. Validate configuration before using
instrument-server validate config configs/test_instrument.yaml
instrument-server validate api apis/test_api.yaml

# 3. Test instrument with custom plugin
instrument-server test configs/test_instrument.yaml IDN --plugin ./my_plugin. so

# If test succeeds, start instrument
instrument-server start configs/test_instrument.yaml --plugin ./my_plugin.so

# 4. Run test measurement with debug logging
instrument-server measure scripts/test_measurement.lua --log-level debug

# 5. Check logs for issues
tail -f instrument_server.log
tail -f worker_TestInstrument.log

# 6. Stop and restart instrument if needed
instrument-server stop TestInstrument
instrument-server start configs/test_instrument.yaml --plugin ./my_plugin.so

# 7. Cleanup
instrument-server daemon stop
```

### Example 3: Multi-Instrument Setup

```bash
# 1. Start daemon
instrument-server daemon start

# 2. Discover available plugins
instrument-server plugins

# 3. Start multiple instruments
instrument-server start configs/dac1.yaml
instrument-server start configs/dac2.yaml
instrument-server start configs/dac3.yaml
instrument-server start configs/dmm1.yaml
instrument-server start configs/dmm2.yaml
instrument-server start configs/scope1.yaml

# 4. Verify all running
instrument-server list

# 5. Check individual status
for inst in DAC1 DAC2 DAC3 DMM1 DMM2 Scope1; do
    echo "=== $inst ==="
    instrument-server status $inst
done

# 6. Run complex measurement with parallel execution
instrument-server measure scripts/stability_diagram.lua

# 7. Selective shutdown
instrument-server stop Scope1

# 8. Continue with remaining instruments
instrument-server measure scripts/final_measurement.lua

# 9. Complete shutdown
instrument-server daemon stop
```

### Example 4: Troubleshooting

```bash
# 1. Check daemon status
instrument-server daemon status

# If not running, start it
if [ $? -ne 0 ]; then
    instrument-server daemon start
fi

# 2. Validate configuration files
instrument-server validate config configs/problematic_instrument.yaml
instrument-server validate api apis/problematic_api.yaml

# 3. Try starting instrument with debug logging
instrument-server start configs/problematic_instrument.yaml --log-level debug

# 4. Check logs immediately
tail -20 instrument_server.log

# 5. Test specific command
instrument-server test configs/problematic_instrument.yaml IDN

# 6. If plugin issue, try with explicit plugin path
instrument-server start configs/problematic_instrument.yaml \
    --plugin /usr/local/lib/instrument-plugins/visa_builtin.so \
    --log-level debug

# 7. Monitor worker log
tail -f worker_ProblematicInstrument.log

# 8. Check IPC issues (Linux)
ls -la /tmp/instrument-server-$USER/

# 9. Clean up if needed
instrument-server daemon stop
rm -rf /tmp/instrument-server-$USER/
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

instrument-server daemon start
if [ $? -ne 0 ]; then
    echo "Failed to start daemon"
    exit 1
fi

instrument-server start configs/dmm1.yaml
if [ $? -ne 0 ]; then
    echo "Failed to start DMM1"
    instrument-server daemon stop
    exit 1
fi

instrument-server measure scripts/measurement.lua
result=$? 

instrument-server daemon stop
exit $result
```

## Environment Variables

### `INSTRUMENT_SCRIPT_SERVER_RPC_PORT`

**Type**: Integer (1-65535)  
**Default**: `8555`  
**Description**: Port number for the HTTP RPC server on localhost

The RPC server provides programmatic API access for embedding and automation.

**Example:**

```bash
# Start daemon with custom RPC port
export INSTRUMENT_SCRIPT_SERVER_RPC_PORT=9000
instrument-server daemon start
```

# RPC endpoint now available at <http://127.0.0.1:9000/rpc>

## See Also

- [Main README](../README.md) - Getting started and overview
- [Configuration Guide](CONFIGURATION.md) - Writing configuration files
- [Architecture](ARCHITECTURE.md) - System design and components
- [Plugin Development](PLUGIN_DEVELOPMENT.md) - Writing instrument plugins
- [Synchronization Protocol](SYNCHRONIZATION.md) - Parallel execution details
