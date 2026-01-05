# Falcon Instrument Script Server

A modular, process-isolated system for controlling scientific instruments for laboratory automation.

## Features

- **Process Isolation**: Each instrument runs in a separate worker process for fault tolerance
- **Plugin Architecture**: Instrument drivers as loadable plugins (VISA, serial, custom SDKs)
- **Lua Scripting**: High-level measurement scripts with runtime contexts
- **Synchronization**:  Parallel execution with precise timing coordination across instruments
- **Cross-Platform**: Works on Linux and Windows

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
instrument-server measure dc my_measurement.lua

# Check status
instrument-server list

# Shutdown
instrument-server daemon stop
```

## Documentation

- **[Configuration Guide](docs/CONFIGURATION.md)** - How to write instrument configurations and API definitions
- **[CLI Usage](docs/CLI_USAGE.md)** - Complete command-line interface reference
- **[Plugin Development](docs/PLUGIN_DEVELOPMENT.md)** - Creating custom instrument drivers
- **[Architecture](docs/ARCHITECTURE.md)** - System design and components
- **[Synchronization](docs/SYNCHRONIZATION.md)** - Parallel execution protocol

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

See the [Configuration Guide](docs/CONFIGURATION.md) for detailed information on the JSON schema.

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
context: call("INSTRUMENT_NAME. SET_VOLTAGE", {voltage = 5.0})
local result = context:call("INSTRUMENT_NAME.MEASURE_VOLTAGE")
print("Measured:  " .. result ..  " V")
EOF

instrument-server measure dc simple_measurement.lua

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

## License

See [LICENSE](LICENSE) for details.

## Contributing

Contributions are welcome! Please see our contribution guidelines.

## Authors

Falcon Autotuning Team
