# Instrument Script Server

A modular, process-isolated system for controlling scientific instruments for laboratory automation.
Our [documentation](https://falcon-autotuning.github.io/instrument-script-server/) can bring you up to speed.

## Features

- **Process Isolation**: Each instrument runs in a separate worker process for fault tolerance
- **Plugin Architecture**: Instrument drivers as loadable plugins (VISA, serial, custom SDKs)
- **Lua Scripting**: High-level measurement scripts with runtime contexts
- **Automatic Result Collection**: All command return values are automatically captured with full traceability
- **Synchronization**:  Parallel execution with precise timing coordination across instruments
- **Cross-Platform**: Works on Linux and Windows

## Quick Start

```bash
make build

# Start the server daemon
instrument-script-server daemon start

# Start instruments (customize configs with your instruments)
instrument-script-server start configs/instrument1.yaml
instrument-script-server start configs/instrument2.yaml

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

### Getting Started

Start here if you're new to the Instrument Script Server:

- **[CLI Usage](CLI_USAGE.md)** - Complete command-line interface reference
- **[Configuration Guide](CONFIGURATION.md)** - How to write instrument configurations and API definitions

### Core Concepts

- **[Architecture](ARCHITECTURE.md)** - System design and components
- **[IPC Protocol](IPC_PROTOCOL.md)** - Inter-process communication details
- **[Synchronization](SYNCHRONIZATION.md)** - Parallel execution protocol

### Extension & Integration

- **[Plugin Development](PLUGIN_DEVELOPMENT.md)** - Creating custom instrument drivers
- **[Embedding API](EMBEDDING_API.md)** - Embed the server inside other processes/servers
- **[HTTP RPC Interface](RPC.md)** - Remote procedure call interface for external integrations
- **[Job Scheduling & Staging](JOB_SCHEDULING.md)** - Job handling, staging and NOPs

## Environment Variables

The server supports configuration via environment variables:

### RPC Port Configuration

- **Variable**: `INSTRUMENT_SCRIPT_SERVER_RPC_PORT`
- **Default**: `8555`
- **Description**: Sets the HTTP RPC server port on localhost for API access

### External Lua Measurement Library Path

- **Variable**: `INSTRUMENT_SCRIPT_SERVER_OPT_LUA_LIB`
- **Default**: ``
- **Description**: Sets the path(s) for optional Lua libraries to load for interpreting measurement scripts. Supports:
  - A single directory containing Lua modules
  - A single Lua bundle file
  - Multiple paths separated by semicolons (`;`)
  - Example: `export INSTRUMENT_SCRIPT_SERVER_OPT_LUA_LIB="/path/to/lib1;/path/to/lib2;/path/to/bundle.lua"`

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

## Built-in Validation Tools

The server includes built-in configuration validation:

```bash
# Validate an instrument configuration
instrument-script-server validate config examples/instrument-configurations/agi_34401_config.yaml

# Validate an API definition
instrument-script-server validate api examples/instrument-apis/agi_34401a. yaml
```

## Contributing

Contributions are welcome! Please see our [contribution guidelines](CONTRIBUTING.md).

## License

See [LICENSE](LICENSE) for details.
