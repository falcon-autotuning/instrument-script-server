# Demo Workflow Guide

This guide demonstrates the complete instrument-script-server workflow using mock instruments that don't require any hardware.

## Important Note About Mock Plugin

The `mock_plugin.so` is primarily designed for automated testing. For manual demo purposes, we recommend:
1. Using the example configuration files with simulated connection strings
2. Or following the instructions below which work within the test framework

## Quick Test with Example Files

The simplest way to test the workflow is using the existing example configurations directly in the tests:

### 1. Start the Server Daemon

In one terminal window:

```bash
instrument-server daemon start
```

The daemon will run in the foreground. Keep this terminal open.

### 2. Start a Mock Instrument

In a second terminal window:

```bash
cd examples
instrument-server start demo_instrument.yaml
```

You should see output indicating the instrument has started.

### 3. Check Running Instruments

```bash
instrument-server list
```

You should see `DemoInstrument` in the list.

### 4. Run a Measurement Script

Run the demo measurement script:

```bash
instrument-server measure demo_measurement.lua
```

You'll see log output showing the measurement steps.

### 5. Run with JSON Output

For programmatic parsing:

```bash
instrument-server measure demo_measurement.lua --json
```

This outputs structured JSON data that can be parsed by other tools.

### 6. Check Instrument Status

```bash
instrument-server status DemoInstrument
```

### 7. Stop the Instrument

```bash
instrument-server stop DemoInstrument
```

### 8. Stop the Daemon

Press `Ctrl+C` in the terminal where the daemon is running, or:

```bash
instrument-server daemon stop
```

## Using Multiple Instruments

You can start multiple mock instruments simultaneously:

```bash
# Start first instrument
instrument-server start tests/data/mock_instrument1.yaml

# Start second instrument  
instrument-server start tests/data/mock_instrument2.yaml

# List all running instruments
instrument-server list
```

## Testing with Real Hardware

To test with actual instruments (e.g., Agilent 34401A DMM):

1. Edit the configuration file:
   ```bash
   cp examples/instrument-configurations/agi_34401_config.yaml my_dmm.yaml
   nano my_dmm.yaml  # Update the VISA address to match your instrument
   ```

2. Start the instrument:
   ```bash
   instrument-server start my_dmm.yaml
   ```

3. Modify a measurement script to use your instrument name

## Troubleshooting

### "Connection refused" or "No such file or directory"

Make sure the daemon is running:
```bash
instrument-server daemon start
```

### "Plugin not found"

Verify mock_plugin.so is installed:
```bash
ls -la /usr/local/lib/instrument-plugins/mock_plugin.so
```

If missing, reinstall:
```bash
cd /path/to/instrument-script-server
sudo make install
```

### "Instrument already exists"

Stop the existing instrument first:
```bash
instrument-server stop DemoInstrument
```

### View Detailed Logs

The daemon logs to stdout. Watch it in the terminal where you started the daemon.

## Next Steps

- Explore other example scripts in `examples/scripts/`
- Read [CLI_USAGE.md](../docs/CLI_USAGE.md) for all available commands
- See [CONFIGURATION.md](../docs/CONFIGURATION.md) to create your own instrument configurations
- Check [PLUGIN_DEVELOPMENT.md](../docs/PLUGIN_DEVELOPMENT.md) to create custom instrument drivers
