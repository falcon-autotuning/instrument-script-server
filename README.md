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
- **[Embedding API](docs/EMBEDDING_API.md)** - How to embed the server inside other processes/servers (new)
- **[Job Scheduling & Staging](docs/JOB_SCHEDULING.md)** - New job handling, staging and NOPs (new)

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
- **Description**: Sets the path for an optional lua library to load for interpreting measurement scripts. This supports either the directory of a larger package or just a file.

## Installation

### Dependencies

Required:

- CMake 3.20+
- C++17 compiler
- Lua 5.3+ or LuaJIT
- sol2 (Lua C++ bindings)
- spdlog (logging)
- nlohmann_json (JSON parsing)
- yaml-cpp (YAML parsing)
- Google Test (for testing)

Optional:

- NI-VISA (for VISA instruments)

### Build

```bash
git clone https://github.com/falcon-autotuning/instrument-script-server.git
cd instrument-script-server
make build
sudo cmake --install .
```

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
