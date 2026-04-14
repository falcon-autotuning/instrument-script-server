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
# Install dependencies (Ubuntu 22.04)
sudo make setup-ubuntu

# Or for Arch Linux
sudo make setup-arch

# Build and install
make build
sudo make install

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

### Teal/TypeScript Support

The server now supports statically-typed measurement scripts using Teal (TypeScript for Lua):

- **[Teal Migration Guide](TEAL_MIGRATION.md)** - New script format for Teal static typing
- **[Type Manifest](TEAL_TYPE_MANIFEST.md)** - Type checking and validation for measurement scripts

## Important: Measurement Script Requirements

!!! warning "Return Statement Required"
    Measurement scripts using the `main()` function format **must** include a return statement (can be `nil`). This is mandatory for proper error handling and result collection.

See the [Teal Migration Guide](TEAL_MIGRATION.md) for complete script format requirements.

The server now supports a job-based measurement lifecycle and staging area for measurement artifacts prior to deployment:

- Jobs represent a complete measurement run (script, parameters, artifacts).
- Jobs can be scheduled, staged (prepared), deployed (pushed to workers), and run.
- A lightweight NOP (no-op) command family was added to the command language to support dry-run, timing placeholders, and synchronization-only markers.

See docs/JOB_SCHEDULING.md for full details on the job lifecycle, states, CLI and embedding API usage, and semantics of the new NOP commands.

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

### New Format (Teal-Compatible)

The server uses blocking execution with structured return types for type safety:

```lua
-- Define a main function that receives the runtime context
function main(ctx, voltage)
    ctx:log("Starting measurement")
    
    -- ctx:call() returns MeasurementResponse objects with metadata
    local current_resp = ctx:call("DMM.MEASURE")
    local current = current_resp:value()  -- Extract actual value
    
    -- Perform math operations
    local power = current * voltage
    
    -- Or use built-in operations on measurements
    local adjusted = current_resp:add_offset(-0.5):multiply_gain(2.0)
    
    return adjusted:value()
end
```

**MeasurementResponse Structure:**
- `instrument()` - Returns instrument name
- `verb()` - Returns command name
- `type()` - Returns value type ("float", "integer", "string", "boolean", "buffer")
- `value()` - Returns the actual value
- `add_offset(offset)` - Adds offset to numeric values
- `multiply_gain(gain)` - Multiplies numeric values by gain

See [Teal Migration Guide](docs/TEAL_MIGRATION.md) for complete details.

### Legacy Format (Deprecated)

Scripts without a `main` function continue to work but emit deprecation warnings:

```lua
-- Old format: still supported (deprecated)
context:log("Starting measurement")
local result = context:call("INSTRUMENT.COMMAND")
        ctx:error("Measurement failed: no result")
        return nil
    end
    
    -- Results are automatically collected from context:call()
    return nil  -- Optional return value
end
```

### Teal Static Typing with Type Manifest

For Teal scripts with typed parameters, use a **type manifest** to declare parameter types:

```teal
-- Teal script with typed parameters
record RuntimeContext
    log: function(RuntimeContext, string)
    call: function(RuntimeContext, string, {any:any}): any
end

function main(ctx: RuntimeContext, voltage: number, sampleRate: number): nil
    ctx:log("Voltage: " .. tostring(voltage))
    ctx:log("Sample rate: " .. tostring(sampleRate))
    return nil
end
```

Generate a type manifest from your Teal file:

```bash
lua scripts/teal_manifest_generator.lua measurement.tl > manifest.json
```

Pass the manifest when running the measurement:

```bash
instrument-script-server measure measurement.lua \
    --json \
    --globals '{"voltage": 5.0, "sampleRate": 1000}' \
    --type-manifest-file manifest.json
```

**Benefits:**
- ✓ Compile-time type checking with Teal
- ✓ Runtime parameter validation
- ✓ Clear parameter contracts
- ✓ Better error messages
- ✓ Missing parameter detection
- ✓ Unused global warnings

See [TEAL_TYPE_MANIFEST.md](docs/TEAL_TYPE_MANIFEST.md) for complete documentation.

**Key features:**
- **Context parameter**: The `main(ctx)` function signature receives the runtime context
- **Typed parameters**: Additional parameters can be passed with type validation
- **Global variables**: Spec variables are injected as globals (with warnings)
- **Explicit error handling**: Use `context:error(message)` to report script errors
- **Automatic result collection**: All `context:call()` operations are automatically captured
- **Return statement**: Main function must have a return statement (can be `nil`)

### Backward Compatibility

Scripts without a `main` function continue to work using the old format:
```lua
-- Old format: executes at script load time
context:log("Starting measurement")
local result = context:call("INSTRUMENT.COMMAND", {param = value})
```

## Installation

**Quick Install:**

```bash
# Ubuntu 22.04 LTS
sudo make setup-ubuntu
make build
sudo make install

# Arch Linux  
sudo make setup-arch
make build
sudo make install
```

For detailed installation instructions, troubleshooting, and platform-specific notes, see [INSTALL.md](INSTALL.md).

### Dependencies

See [INSTALL.md](INSTALL.md) for detailed dependency installation instructions.

### Build

```bash
git clone https://github.com/falcon-autotuning/instrument-script-server.git
cd instrument-script-server
make clean  # Clean any previous builds
make build  # Build the project
sudo make install  # Install binaries and libraries-only)
git clone --depth 1 --branch v3.5.0 https://github.com/ThePhD/sol2.git /tmp/sol2
sudo mkdir -p /usr/local/include/sol
sudo cp -r /tmp/sol2/include/sol/* /usr/local/include/sol/
```

**Windows:**
Dependencies are managed via vcpkg (see `vcpkg.json`). The CI pipeline handles Windows builds automatically.

### Build

```bash
git clone https://github.com/falcon-autotuning/instrument-script-server.git
cd instrument-script-server
make clean  # Clean any previous builds
make build  # Build the project
sudo make install  # Install binaries and libraries
```

### Running Tests

```bash
cd build

# Run unit tests
make test_unit

# Run integration tests
make test_integration

# Run performance benchmarks
make test_perf
```

**Note:** All tests must pass before committing changes. Both unit and integration tests validate the new main function format, deprecation warnings, and error handling.

### Verify Installation

```bash
instrument-script-server --help
instrument-script-server plugins
```

If you encounter library loading errors, run `sudo ldconfig` to update the dynamic linker cache. See [INSTALL.md](INSTALL.md#troubleshooting) for more troubleshooting tips.

## Configuration

Configuration files are located in the `examples/` folder:

- **[examples/instrument-configurations/](examples/instrument-configurations/)** - Sample instrument configurations
- **[examples/instrument-apis/](examples/instrument-apis/)** - Sample API definitions
- **[examples/scripts/](examples/scripts/)** - Sample measurement scripts

See the [Configuration Guide](CONFIGURATION.md) for detailed information on the JSON schema.

## Example Workflow

```bash
# 1. Start the daemon
instrument-script-server daemon start

# 2. Start your instruments (modify example configs with your connection details)
instrument-script-server start examples/instrument-configurations/agi_34401_config.yaml
instrument-script-server start examples/instrument-configurations/dso9254a_config.yaml

# 3. Write and run a measurement script
cat > simple_measurement.lua << 'EOF'
-- Set voltage and measure
context: call("INSTRUMENT_NAME.SET_VOLTAGE", {voltage = 5.0})
local result = context:call("INSTRUMENT_NAME.MEASURE_VOLTAGE")
print("Measured:  " .. result ..  " V")
EOF

instrument-script-server measure simple_measurement.lua

# 4. Check status
instrument-script-server list
instrument-script-server status INSTRUMENT_NAME

# 5. Stop when done
instrument-script-server daemon stop
```

## Built-in Validation Tools

The server includes built-in configuration validation:

```bash
# Validate an instrument configuration
instrument-script-server validate config examples/instrument-configurations/agi_34401_config.yaml

# Validate an API definition
instrument-script-server validate api examples/instrument-apis/agi_34401a. yaml
```

## Testing

```bash
cd build

# Run specific test categories
make test_unit           # Fast unit tests
make test_integration    # Integration tests
make test_perf          # Performance benchmarks
```

## Contributing

Contributions are welcome! Please see our [contribution guidelines](CONTRIBUTING.md).

## License

See [LICENSE](LICENSE) for details.
