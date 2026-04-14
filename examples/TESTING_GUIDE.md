# Testing the Instrument Script Server

## Available Example Files

Yes! The repository includes several files you can use to test the workflow:

### Configuration Files

**Working Examples (Real Hardware Required):**
- `examples/instrument-configurations/agi_34401_config.yaml` - Agilent 34401A Digital Multimeter
- `examples/instrument-configurations/dso9254a_config.yaml` - Keysight DSO9254A Oscilloscope

**Test/Mock Configurations:**
- `tests/data/mock_instrument1.yaml` - Mock instrument for automated testing
- `tests/data/mock_instrument2.yaml` - Second mock instrument
- `tests/data/mock_instrument3.yaml` - Third mock instrument

### Measurement Scripts

- `examples/scripts/dc_getset_example.lua` - DC measurement with parallel operations
- `examples/scripts/1d_waveform_example.lua` - 1D waveform acquisition
- `examples/scripts/2d_waveform_example.lua` - 2D waveform acquisition
- `examples/scripts/typed_measurement_example.tl` - Teal (typed Lua) example

### API Definitions

- `examples/instrument-apis/agi_34401a.yaml` - API for Agilent 34401A
- `examples/instrument-apis/dso9254a.yaml.tmpl` - Template for DSO9254A
- `tests/data/mock_api.yaml` - Mock instrument API

## Testing Without Hardware

The mock plugin (`mock_plugin.so`) is installed automatically but is primarily designed for **automated testing**, not interactive manual use. 

For manual testing without hardware, you have these options:

### Option 1: Use Test Framework (Recommended)

Run the integration tests which exercise the full workflow:

```bash
cd build
./tests/integration_tests
```

### Option 2: Examine Example Scripts

Review the example measurement scripts to understand the scripting format:

```bash
cat examples/scripts/dc_getset_example.lua
```

### Option 3: Adapt for Real Hardware

If you have VISA-compatible instruments:

1. Edit a configuration file:
   ```bash
   cp examples/instrument-configurations/agi_34401_config.yaml mydmm.yaml
   nano mydmm.yaml
   ```

2. Update the connection address to match your instrument:
   ```yaml
   connection:
     type: VISA
     address: TCPIP::192.168.1.100::INSTR  # Your instrument's IP
   ```

3. Start the daemon:
   ```bash
   instrument-script-server daemon start
   ```
   
   (Keep this running in one terminal)

4. In another terminal, start your instrument:
   ```bash
   instrument-script-server start mydmm.yaml
   ```

5. Create a simple measurement script:
   ```lua
   function main(ctx)
       ctx:log("Starting measurement")
       
       -- Query instrument ID
       local idn = ctx:call("INSTRUMENT_NAME.IDN")
       ctx:log("ID: " .. idn:value())
       
       -- Your measurement commands here
       
       return nil
   end
   ```

6. Run the measurement:
   ```bash
   instrument-script-server measure my_measurement.lua
   ```

## Testing Real Workflow Components

Even without hardware, you can test these components:

### 1. Configuration Validation

```bash
# Validate an instrument configuration
instrument-script-server validate config examples/instrument-configurations/agi_34401_config.yaml

# Validate an API definition
instrument-script-server validate api examples/instrument-apis/agi_34401a.yaml
```

### 2. Script Syntax

Check if your Lua scripts have correct syntax:

```bash
lua -l examples/scripts/dc_getset_example.lua
```

### 3. Daemon Management

```bash
# Start daemon
instrument-script-server daemon start

# In another terminal:
instrument-script-server daemon status

# Check for any running instruments (will be empty without hardware)
instrument-script-server list

# Stop daemon
instrument-script-server daemon stop
```

## What Each Component Does

From the README workflow:

```bash
# 1. Start server daemon - manages all instruments and workers
instrument-script-server daemon start

# 2. Start instruments - loads configuration and creates worker processes
instrument-script-server start configs/instrument1.yaml
instrument-script-server start configs/instrument2.yaml

# 3. Run measurement - executes Lua script against running instruments
instrument-script-server measure my_measurement.lua

# 4. JSON output - structured output for programmatic use
instrument-script-server measure my_measurement.lua --json
```

## Example Command Outputs

### List Command (with instruments running):
```
Running Instruments:
  - DMM1 (VISA: TCPIP::192.168.1.100::INSTR)
  - SCOPE1 (VISA: TCPIP::192.168.1.101::INSTR)
```

### Status Command:
```
instrument-script-server status DMM1
```
Shows connection state, last command, errors, etc.

### Measurement Output:
```
[2026-02-04 10:00:00.000] [instrument] [info] Starting measurement
[2026-02-04 10:00:00.100] [instrument] [info] Voltage: 5.000 V
[2026-02-04 10:00:00.200] [instrument] [info] Current: 0.025 A
Measurement complete: {"voltage": 5.0, "current": 0.025}
```

## Next Steps

1. **With Hardware**: Follow Option 3 above to connect  to real instruments
2. **Learn More**: Read the documentation:
   - [CLI_USAGE.md](../docs/CLI_USAGE.md) - Complete command reference
   - [CONFIGURATION.md](../docs/CONFIGURATION.md) - How to write configurations
   - [ARCHITECTURE.md](../docs/ARCHITECTURE.md) - System design
3. **Develop**: Create custom plugins following [PLUGIN_DEVELOPMENT.md](../docs/PLUGIN_DEVELOPMENT.md)
