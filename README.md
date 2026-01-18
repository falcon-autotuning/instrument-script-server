# Instrument Script Server

A modular, process-isolated system for controlling scientific instruments for laboratory automation.

## Features

- **Process Isolation**: Each instrument runs in a separate worker process for fault tolerance
- **Plugin Architecture**: Instrument drivers as loadable plugins (VISA, serial, custom SDKs)
- **Lua Scripting**: High-level measurement scripts with runtime contexts
- **Automatic Result Collection**: All command return values are automatically captured with full traceability
- **Synchronization**:  Parallel execution with precise timing coordination across instruments
- **Cross-Platform**: Works on Linux and Windows

## Performance

The Instrument Script Server is designed for high-performance laboratory automation with minimal overhead:

### End-to-End Performance (Best Case)

- **Average Command Latency**: ~200 µs per command
- **Throughput**: ~5,000 commands/second
- **IPC Throughput**: 400,000+ messages/second
- **Sync Barrier Overhead**: <4 µs per synchronization point

### Scalability

- **Concurrent Instruments**: Supports 10+ instruments simultaneously
- **Multi-instrument Commands**: 200 µs average latency with 10 concurrent instruments
- **Setup Time**: ~500 ms per additional instrument

### Use Cases

- Single instrument control: ~200 µs overhead per command
- Complex measurements with parameters: ~220 µs overhead
- Array/large data transfers: ~220 µs overhead
- Multi-instrument parallel execution: Linear scaling up to 10+ instruments

These benchmarks were measured on a standard development machine and represent typical performance. Actual performance may vary based on hardware, instrument drivers, and measurement complexity.

## Quick Start

```bash
# Build and install
make build
sudo cmake --install .

# Start the server daemon
instrument-server daemon start

# Start instruments (customize configs with your instruments)
instrument-server start configs/instrument1.yaml
instrument-server start configs/instrument2.yaml

# Run a measurement
instrument-server measure my_measurement.lua

# Run with JSON output for programmatic parsing
instrument-server measure my_measurement.lua --json

# Check status
instrument-server list

# Shutdown
instrument-server daemon stop
```

## Documentation

Find it [here](https://falcon-autotuning.github.io/instrument-script-server/).

- **[Configuration Guide](docs/CONFIGURATION.md)** - How to write instrument configurations and API definitions
- **[CLI Usage](docs/CLI_USAGE.md)** - Complete command-line interface reference
- **[Plugin Development](docs/PLUGIN_DEVELOPMENT.md)** - Creating custom instrument drivers
- **[Architecture](docs/ARCHITECTURE.md)** - System design and components
- **[Synchronization](docs/SYNCHRONIZATION.md)** - Parallel execution protocol
- **[Embedding API](docs/EMBEDDING_API.md)** - How to embed the server inside other processes/servers
- **[Job Scheduling & Staging](docs/JOB_SCHEDULING.md)** - Job handling, staging and NOPs
- **[Teal Migration Guide](docs/TEAL_MIGRATION.md)** - New script format for Teal static typing (new)

## New / Important: Embedding API

A programmatic API is now available to embed the Instrument Script Server within other servers/processes (for example, a higher-level orchestration server that wants to directly control instruments without launching a separate daemon process). See docs/EMBEDDING_API.md for API surface, patterns, and examples (C++ and Lua).

Key points:

- You can create an in-process server instance, register instruments or instrument factories, and submit measurement jobs programmatically.
- Embedding supports the same IPC, worker-process model and Lua runtime, but runs the ServerDaemon API inside your process.
- Embedding is designed to be non-blocking: the host process receives callbacks or futures for job completion.

## New / Important: Job scheduling, staging, and NOPs

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

The server now supports a new script format designed for Teal static typing and compilation:

```lua
-- Define a main function that receives the runtime context
function main(ctx)
    -- Access context parameter
    ctx:log("Starting measurement")
    
    -- Use context:call() for instrument commands
    local result = ctx:call("INSTRUMENT.COMMAND", {param = value})
    
    -- Use context:error() to report failures
    if not result then
        ctx:error("Measurement failed: no result")
        return nil
    end
    
    -- Results are automatically collected from context:call()
    return nil  -- Optional return value
end
```

**Key features:**
- **Context parameter**: The `main(ctx)` function signature receives the runtime context
- **Global variables**: Spec variables are injected as globals and accessible in main
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

### Dependencies

Required:

- CMake 3.20+
- C++17 compiler (Clang or GCC recommended)
- Lua 5.3+ or LuaJIT
- sol2 (Lua C++ bindings) - v3.5.0+
- spdlog (logging)
- nlohmann_json (JSON parsing)
- yaml-cpp (YAML parsing)
- Google Test (for testing)
- Boost (boost-interprocess, boost-date-time)

Optional:

- NI-VISA (for VISA instruments)

### Installing Dependencies

**Arch Linux:**
```bash
sudo pacman -S base-devel git cmake ninja clang lld llvm lua luajit spdlog nlohmann-json yaml-cpp gtest boost

# Install sol2 (header-only)
git clone --depth 1 --branch v3.5.0 https://github.com/ThePhD/sol2.git /tmp/sol2
sudo mkdir -p /usr/local/include/sol
sudo cp -r /tmp/sol2/include/sol/* /usr/local/include/sol/
```

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install build-essential git cmake ninja-build clang liblua5.3-dev \
    libspdlog-dev nlohmann-json3-dev libyaml-cpp-dev libgtest-dev \
    libboost-date-time-dev libboost-interprocess-dev

# Install sol2 (header-only)
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
sudo cmake --install .
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
instrument-server --help
instrument-server plugins
```

## Configuration

Configuration files are located in the `examples/` folder:

- **[examples/instrument-configurations/](examples/instrument-configurations/)** - Sample instrument configurations
- **[examples/instrument-apis/](examples/instrument-apis/)** - Sample API definitions
- **[examples/scripts/](examples/scripts/)** - Sample measurement scripts

See the [Configuration Guide](CONFIGURATION.md) for detailed information on the JSON schema.

## Example Workflow

```bash
# 1. Start the daemon
instrument-server daemon start

# 2. Start your instruments (modify example configs with your connection details)
instrument-server start examples/instrument-configurations/agi_34401_config.yaml
instrument-server start examples/instrument-configurations/dso9254a_config.yaml

# 3. Write and run a measurement script
cat > simple_measurement.lua << 'EOF'
-- Set voltage and measure
context: call("INSTRUMENT_NAME.SET_VOLTAGE", {voltage = 5.0})
local result = context:call("INSTRUMENT_NAME.MEASURE_VOLTAGE")
print("Measured:  " .. result ..  " V")
EOF

instrument-server measure simple_measurement.lua

# 4. Check status
instrument-server list
instrument-server status INSTRUMENT_NAME

# 5. Stop when done
instrument-server daemon stop
```

## Built-in Validation Tools

The server includes built-in configuration validation:

```bash
# Validate an instrument configuration
instrument-server validate config examples/instrument-configurations/agi_34401_config.yaml

# Validate an API definition
instrument-server validate api examples/instrument-apis/agi_34401a. yaml
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
