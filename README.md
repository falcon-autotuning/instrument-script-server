# Instrument Script Server

A modular, process-isolated system for controlling scientific instruments for laboratory automation.
Our [documentation](https://falcon-autotuning.github.io/instrument-script-server/) can bring you up to speed.

## Features

- **Process Isolation**: Each instrument runs in a separate worker process for fault tolerance
- **Plugin Architecture**: Instrument drivers as loadable plugins (VISA, custom, ...)
- **Lua Scripting**: High-level measurement scripts with runtime contexts
- **Automatic Result Collection**: All command return values are automatically captured with full traceability
- **Synchronization**:  Parallel execution with precise timing coordination across instruments

## Quick Start

```bash
make build

# Start the server daemon
instrument-script-server daemon start

# Start instruments (customize configs with your instruments)
instrument-script-server inst start configs/instrument1.yaml
instrument-script-server inst start configs/instrument2.yaml

# Run a measurement
instrument-script-server measure my_measurement.lua

# Run with JSON output for programmatic parsing
instrument-script-server measure my_measurement.lua --json

# Check status
instrument-script-server list

# Shutdown
instrument-script-server daemon stop
```

## Documentation Guide

Start here if you're new to the Instrument Script Server:

- **[CLI Usage](CLI_USAGE.md)** - Complete command-line interface reference
- **[Configuration Guide](CONFIGURATION.md)** - How to write instrument configurations and API definitions
- **[Plugin Development](PLUGIN_DEVELOPMENT.md)** - Creating custom instrument drivers

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

## Measurement Script Structure

### Measurement Scripts

Measurement scripts are written in Lua and interact with instruments using structured command objects.

#### Basic Example

```lua
function main(ctx)
    ctx:log("Starting measurement")

    local cs = instrument_call_stack.new{
        instrument = "DMM",
        command = "MEASURE"
    }

    local resp = ctx:call(cs)

    local value = resp:value()
    ctx:log("Measured: " .. tostring(value))

    return value
end
````

#### CallStack Commands

Commands are constructed explicitly using `instrument_call_stack.new{}`:

- `instrument` – Instrument name
- `command` – API command
- `channel` *(optional)* – Channel index
- `params` *(optional)* – Command parameters

Example with parameters:

```lua
local cs = instrument_call_stack.new{
    instrument = "PSU",
    command = "SET_VOLTAGE",
    params = { voltage = 5.0 }
}
```

***

#### Return Values

`ctx:call()` returns a **MeasurementResponse** object:

- `value()` → actual result
- `type()` → return type
- `instrument()` → instrument name
- `verb()` → command name

Example:

```lua
local resp = ctx:call(cs)
local val = resp:value()
```

***

#### Teal Support (Optional)

Teal can be used for type-checked scripts, but must be compiled to Lua before execution.

ISS provides runtime validation through **type manifest files**, which describe expected script inputs.

See:

- [TEAL_TYPE_MANIFEST](TEAL_TYPE_MANIFEST.md)

***

## Configuration

Configuration files are located in the `test/data/` folder:

See the [Configuration Guide](CONFIGURATION.md) for detailed information on the JSON schema.

## Contributing

Contributions are welcome! Please see our [contribution guidelines](CONTRIBUTING.md).

## License

See [LICENSE](LICENSE) for details.
