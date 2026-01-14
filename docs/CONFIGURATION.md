# Configuration Guide

Complete guide to configuring instruments and writing API definitions.

## Overview

The Instrument Script Server uses two types of configuration files:

1. **Instrument Configuration** - Defines a specific instrument instance with connection details
2. **Instrument API Definition** - Defines the command set and protocol for an instrument type

Both use YAML format and are validated against JSON schemas.

## Instrument Configuration

### Purpose

An instrument configuration file describes **how to connect to a specific instrument instance** on your system.
It references an API definition and provides the physical connection details.

### Schema Reference

The configuration is validated against [`schemas/instrument_configuration.schema.json`](../schemas/instrument_configuration.schema.json).

### Required Fields

```yaml
name:  INSTRUMENT_NAME          # Unique identifier (uppercase with underscores)
api_ref: path/to/api.yaml     # Path to the API definition file
connection:                     # Connection details
  type: VISA                   # Connection type:  VISA or Custom
  address: "..."              # Connection address
io_config:                    # Configuration for each IO port
  port_name: 
    type: float
    role: input|output|inout
    unit: V
    offset: 0
    scale: 1
```

### Field Descriptions

#### `name` (required)

**Type**: String (pattern: `^[A-Z][A-Z0-9_]*$`)

**Description**: Unique identifier for this instrument instance.  
This is how you'll reference the instrument in measurement scripts.

**Examples**:

- `DMM1`, `DMM2` - Multiple instances of the same instrument type
- `SCOPE_LEFT`, `SCOPE_RIGHT` - Descriptive names
- `DAC_GATE1`, `DAC_BIAS` - Function-based names

##### `api_ref` (required)

**Type**: String (file path or file:// URI)

**Description**: Path or URI that points to the instrument API definition file (the API describes the commands, IO and protocol for the instrument type). The server supports several forms and resolves them according to the rules below.

Supported forms

- Absolute file-system path
  - Example: `/usr/local/share/instrument-apis/keithley_2400.yaml`
- Relative path (recommended when your API file is co-located with the instrument configuration)
  - Example: `../apis/agi_34401a.yaml`
- `file://` URI (supported; treated as a file-system path)
  - Examples:
    - `file:///usr/local/share/instrument-apis/dmm.yaml`
    - `file://./apis/agi_34401a.yaml`

Resolution rules

1. If `api_ref` is a `file://` URI, the `file://` scheme is stripped and the remainder is treated as a file-system path (special handling exists for typical Windows `file:///C:/...` forms).
2. If the resulting path is absolute, it is used as-is. The server requires that the file exists.
3. If the resulting path is relative, the server attempts to resolve it in two places (in this order):
   a. The directory containing the instrument configuration file (i.e., the parent directory of the configuration file you passed to `instrument-server start ...`). This is the preferred/primary location and lets you keep the API file next to its configuration.
   b. If (a) does not exist, the server falls back to resolving the relative path against the server process current working directory (cwd). This preserves backward compatibility with existing workflows and test expectations that used repo-root/cwd-relative paths.
4. If the file cannot be found in either location, instrument creation fails with a clear error indicating the preferred (config-relative) path that was checked, e.g.:

   ```
   API definition file not found: /path/to/configs/../apis/myapi.yaml
   ```

5. If the file is found, the resolved path is normalized (canonicalized) before being used, so `../` and `./` fragments are collapsed and the server uses the canonical absolute path.

Notes & best practices

- For reproducibility, prefer placing the API definition in the same repo/directory tree as the instrument configuration and use a relative path (this makes configurations portable between machines).
- If you distribute API definitions as part of a system installation, use absolute paths (or file:// URIs) to the installed location.
- The `instrument-server validate api <file>` command is available to validate an API definition YAML independently; use this before starting instruments to catch schema errors early.
- Error messages from the server are intentionally informative; when a referenced API cannot be found the server will log the attempted config-relative path to aid debugging.

Examples

```yaml
# co-located API (preferred)
name: DMM1
api_ref: ../apis/agi_34401a.yaml
connection:
  type: VISA
  address: "TCPIP::192.168.0.100::INSTR"
```

```yaml
# absolute path
name: DMM2
api_ref: /usr/local/share/instrument-apis/keithley_2400.yaml
connection:
  type: VISA
```

```yaml
# file:// URI
name: DMM3
api_ref: file:///etc/instrument-apis/dmm.yaml
connection:
  type: VISA
```

#### `connection` (required)

**Type**: Object

**Description**:  Specifies how to physically connect to the instrument.

##### `connection.type` (required)

**Type**: String (enum: `VISA` | `Custom`)

**Description**: The connection protocol type.

- **`VISA`**: For instruments using VISA protocol (GPIB, USB, LAN, Serial via VISA)
- **`Custom`**: For custom protocols implemented in plugins

##### `connection.address` (optional)

**Type**: String

**Description**: The connection address.  Format depends on the connection type.

**VISA Address Examples**:

- GPIB: `GPIB0::15::INSTR`
- LAN/Ethernet: `TCPIP::192.168.0.100::INSTR`
- USB:  `USB0::0x1234::0x5678::SN123:: INSTR`
- Serial (via VISA): `ASRL1::INSTR`

**Custom Address Examples**:

- `/dev/ttyUSB0` (serial port)
- `COM3` (Windows serial port)
- `192.168.1.10: 5025` (TCP socket)

##### `connection.baudrate` (optional)

**Type**: Integer

**Description**: Baud rate for serial connections.

**Example**: `9600`, `115200`

##### `connection.custom` (optional)

**Type**: Object

**Description**: Additional custom connection parameters as key-value pairs.

**Example**:

```yaml
connection:
  type: Custom
  custom:
    device: "/dev/ttyUSB0"
    baudrate: 115200
    parity: "none"
    stop_bits: 1
```

#### `startup` (optional)

**Type**: Object

**Description**: Configuration for instrument startup behavior.

##### `startup.init_commands` (optional)

**Type**: Array of strings

**Description**: List of commands to send to the instrument immediately after connection.

**Example**:

```yaml
startup:
  init_commands:
    - "*RST"                    # Reset instrument
    - "*CLS"                    # Clear status
    - "SYST:REM"               # Set to remote mode
```

##### `startup.delay_ms` (optional)

**Type**: Integer

**Description**: Delay in milliseconds to wait after connecting before sending commands.

**Example**: `1000` (wait 1 second after connection)

#### `io_config` (required)

**Type**: Object

**Description**: Configuration for each IO port defined in the API.  
Each IO port that has role `input`, `output`, or `inout` must be configured here.

Each IO configuration must specify:

##### `type` (required)

**Type**: String (enum: `int` | `float` | `bool` | `string`)

**Description**: Data type for this IO port.  
Must match the type defined in the API.

##### `role` (required)

**Type**: String (enum: `input` | `output` | `inout`)

**Description**: Direction of data flow.  Must match the role defined in the API.

- **`input`**: Data flows into the instrument (e.g., voltage to set)
- **`output`**: Data flows out from the instrument (e.g., measured current)
- **`inout`**:  Bidirectional (e.g., configurable GPIO)

##### `unit` (optional)

**Type**: String

**Description**: Physical unit for this IO port (e.g., `V`, `A`, `Hz`).

##### `offset` (optional)

**Type**: Number

**Description**: Offset value applied to the data:  
`actual_value = raw_value * scale + offset`

**Default**: `0`

##### `scale` (optional)

**Type**: Number

**Description**: Scale factor applied to the data:
`actual_value = raw_value * scale + offset`

**Default**: `1`

### Complete Examples

#### Example 1: Digital Multimeter (VISA over LAN)

```yaml
name: DMM1
api_ref:  examples/instrument-apis/agi_34401a.yaml
connection:
  type: VISA
  address: "TCPIP::192.168.0.100::INSTR"
  timeout: 5000
startup:
  init_commands: 
    - "*RST"
    - "*CLS"
  delay_ms: 500
io_config:
  voltage: 
    type: float
    role: input
    unit: V
    offset: 0
    scale: 1
  measured_voltage:
    type: float
    role: output
    unit: V
    offset: 0
    scale: 1
```

#### Example 2: Oscilloscope with Multiple Channels

```yaml
name: SCOPE1
api_ref: examples/instrument-apis/dso9254a_extended.yaml
connection:
  type: VISA
  address: "USB0::0x0957::0x179B::MY12345678::INSTR"
io_config:
  analog1_waveform:
    type: float
    role: output
    unit: V
  analog2_waveform: 
    type: float
    role:  output
    unit: V
  analog3_waveform:
    type: float
    role: output
    unit: V
  analog4_waveform:
    type: float
    role: output
    unit: V
  timebase:
    type: float
    role: setting
    unit: s
```

#### Example 3: Custom Serial Instrument

```yaml
name: CUSTOM_DEVICE
api_ref: apis/custom_serial_device.yaml
connection:
  type: Custom
  custom:
    device: "/dev/ttyUSB0"
    baudrate:  115200
io_config:
  setpoint:
    type: float
    role: input
    unit: degC
    offset: 0
    scale: 1
  temperature:
    type: float
    role: output
    unit: degC
    offset: 0
    scale:  1
```

---

## Instrument API Definition

### Purpose

An API definition file describes **what commands an instrument type supports** and how to send those commands.  It's protocol-agnostic and reusable across multiple instances of the same instrument model.

### Schema Reference

The API is validated against [`schemas/instrument_api.schema.json`](../schemas/instrument_api.schema.json).

### Required Fields

```yaml
api_version: "1.0.0"           # API schema version
instrument:                      # Instrument metadata
  vendor:  "Vendor Name"
  model: "Model Number"
  identifier: "UNIQUE_ID"
protocol:                       # Protocol type
  type:  VISA
io:                             # IO port definitions
  - name: port_name
    type: float
    role: input|output|setting
    unit: V
    description: "..."
commands:                      # Command definitions
  COMMAND_NAME:
    template: "SCPI: COMMAND {param}"
    description: "..."
    parameters: [...]
    outputs: [...]
```

### Field Descriptions

#### `api_version` (required)

**Type**: String (pattern: `^[0-9]+\.[0-9]+\.[0-9]+$`)

**Description**: Semantic version of the API schema format.

**Example**: `1.0.0`

#### `instrument` (required)

**Type**: Object

**Description**: Metadata about the instrument.

##### `instrument.vendor` (required)

**Type**: String

**Description**:  Manufacturer name.

**Examples**:  `Keysight`, `Agilent`, `Rohde & Schwarz`, `Tektronix`

##### `instrument.model` (required)

**Type**: String

**Description**: Model number or identifier.

**Examples**: `DSO9254A`, `34401A`, `SMU2450`

##### `instrument.identifier` (required)

**Type**: String (pattern: `^[A-Z][A-Z0-9_]*$`)

**Description**: Unique identifier for this API definition.

**Examples**: `DMM1`, `SCOPE1`, `DAC_API`

##### `instrument.desc` (optional)

**Type**: String

**Description**: Human-readable description of the instrument.

**Example**: `High-Performance Oscilloscope with 4 analog channels`

#### `protocol` (required)

**Type**: Object

**Description**: Protocol configuration.

##### `protocol.type` (required)

**Type**: String (enum: `VISA` | `Custom`)

**Description**: Communication protocol.

- **`VISA`**: Standard VISA protocol (plugin automatically uses VISA commands)
- **`Custom`**: Custom protocol (requires a plugin implementation)

#### `io` (required)

**Type**: Array of objects

**Description**:  Defines all IO ports (inputs, outputs, settings, triggers, etc.) for the instrument.

Each IO port object has:

##### `io[].name` (required)

**Type**: String (pattern: `^[a-z][a-z0-9_]*$`)

**Description**: IO port name (lowercase with underscores).

**Examples**: `voltage`, `current`, `frequency`, `trigger_level`

##### `io[].type` (required)

**Type**: String (enum: `int` | `float` | `bool` | `string` | `array<float>` | `array<int>`)

**Description**: Data type for this IO port.

##### `io[].role` (required)

**Type**: String (enum: `input` | `output` | `inout` | `setting` | `trigger-in` | `trigger-out` | `clock-in` | `clock-out`)

**Description**: The role/direction of this IO port.

- **`input`**: Values written to the instrument
- **`output`**: Values read from the instrument
- **`inout`**: Bidirectional
- **`setting`**: Configuration parameter
- **`trigger-in`**: Trigger input
- **`trigger-out`**: Trigger output
- **`clock-in`**: Clock input
- **`clock-out`**: Clock output

##### `io[].unit` (optional)

**Type**: String

**Description**: Physical unit.

**Examples**: `V`, `A`, `Hz`, `s`, `degC`, `Ohm`

##### `io[].description` (optional)

**Type**: String

**Description**: Human-readable description of the IO port.

#### `channel_groups` (optional)

**Type**: Array of objects

**Description**:  Defines groups of channels (e.g., oscilloscope channels 1-4).

See the [dso9254a. yaml. tmpl](../examples/instrument-apis/dso9254a.yaml.tmpl) example for usage.

#### `commands` (required)

**Type**: Object

**Description**:  Defines all commands the instrument supports.  Each key is a command name, and each value is a command definition.

##### Command Name Format

**Pattern**: `^[A-Z][A-Z0-9_]*$`

**Examples**:  `SET_VOLTAGE`, `MEASURE_CURRENT`, `GET_STATUS`

##### Command Object

Each command has:

###### `template` (required)

**Type**: String

**Description**:  The actual command string sent to the instrument.  Parameters in curly braces `{param}` are substituted at runtime.

**VISA/SCPI Examples**:

- `*IDN?` (identity query)
- `:SOUR:VOLT {voltage}` (set voltage)
- `:MEAS:VOLT: DC?` (measure voltage)
- `:CHAN{channel}:RANG {range}` (set channel range, using channel group)

###### `description` (optional)

**Type**: String

**Description**: Human-readable description of what the command does.

###### `parameters` (optional)

**Type**: Array of objects

**Description**:  Input parameters for the command.

Each parameter object:

```yaml
- name: voltage          # Parameter name
  type: float           # Data type
  description: "..."    # Description (optional)
  unit: V              # Physical unit (optional)
```

Alternatively, you can reference an IO port:

```yaml
- io:  voltage           # References the IO port named "voltage"
```

###### `outputs` (optional)

**Type**: Array of strings

**Description**: List of IO port names that this command produces values for.

**Example**:

```yaml
outputs:  [measured_voltage]   # This command produces a value for the "measured_voltage" IO port
```

###### `returns` (optional)

**Type**: String (enum: `void` | `int` | `float` | `bool` | `string` | `array<float>` | `array<int>`)

**Description**: The return type of the command.  Use `void` for commands that don't return a value.

###### `query` (optional)

**Type**: Boolean

**Description**: If `true`, this command is a query (reads data from the instrument). If `false` or omitted, it's a write command.

###### `channel_group` (optional)

**Type**: String

**Description**: If this command operates on a channel, specify the channel group name here.

### Complete Examples

#### Example 1: Simple Digital Multimeter API

```yaml
api_version: "1.0.0"
instrument:
  vendor: "Agilent"
  model: "34401A"
  identifier: "DMM_API"
  desc: "6. 5 Digit Digital Multimeter"

protocol:
  type: "VISA"

io:
  - name: voltage
    type: float
    role: input
    description: "Voltage to set"
    unit: "V"
  
  - name: measured_voltage
    type: float
    role:  output
    description: "Measured DC voltage"
    unit: "V"
  
  - name: range
    type: float
    role: setting
    description: "Measurement range"
    unit: "V"

commands:
  IDN:
    template: "*IDN?"
    description: "Query instrument identification"
    parameters: []
    outputs: []
    returns: string
    query: true
  
  RESET:
    template: "*RST"
    description: "Reset instrument to default state"
    parameters: []
    outputs: []
    returns:  void
  
  SET_VOLTAGE:
    template:  ":SOUR:VOLT {voltage}"
    description: "Set output voltage"
    parameters:
      - io: voltage
    outputs: []
    returns: void
  
  MEASURE_VOLTAGE:
    template: ": MEAS:VOLT:DC?"
    description: "Measure DC voltage"
    parameters:  []
    outputs: [measured_voltage]
    returns: float
    query: true
  
  SET_RANGE:
    template:  ":VOLT:RANG {range}"
    description: "Set measurement range"
    parameters:
      - io:  range
    outputs: []
    returns: void
  
  GET_RANGE:
    template: ": VOLT:RANG?"
    description: "Query current measurement range"
    parameters: []
    outputs: [range]
    returns: float
    query: true
```

#### Example 2: Oscilloscope with Channel Groups

```yaml
api_version: "1.0.0"
instrument:
  vendor: "Keysight"
  model: "DSO9254A"
  identifier: "SCOPE_API"
  desc: "High-Performance Oscilloscope"

protocol:
  type: "VISA"

channel_groups:
  - name:  analog
    description: "Analog input channels"
    channel_parameter: 
      name: channel
      type: int
      min: 1
      max: 4
      description: "Oscilloscope channel number (1-4)"
    io_types:
      - suffix: waveform
        type: float
        role: output
        unit: "V"
        description: "Waveform data"
      - suffix: voltage_range
        type: float
        role: setting
        unit: "V"
        description: "Voltage range in volts"

io:
  - name: timebase
    type: float
    role: setting
    description: "Global timebase setting"
    unit: "s"
  
  # The channel group automatically creates: 
  # analog1_waveform, analog2_waveform, analog3_waveform, analog4_waveform
  # analog1_voltage_range, analog2_voltage_range, etc. 

commands:
  GET_WAVEFORM:
    template: ":WAV:DATA?  {analog}"
    description: "Retrieve waveform data from specified channel"
    parameters:  []
    channel_group: analog
    outputs: [waveform]
    returns: array<float>
    query: true
  
  SET_VOLTAGE_RANGE: 
    template: ":CHAN{analog}:RANG {value}"
    description: "Set voltage range for channel"
    parameters:
      - name: value
        type: float
        description: "Voltage range in volts"
        unit: "V"
    channel_group: analog
    outputs:  [voltage_range]
    returns:  void
  
  SET_TIMEBASE:
    template: ":TIM:SCAL {timebase}"
    description: "Set timebase (time per division)"
    parameters:
      - io: timebase
    outputs: []
    returns: void
```

---

## Configuration Validation

The server includes built-in validation tools to check your configuration files against the JSON schemas.

### Validating Configurations

```bash
# Validate an instrument configuration
instrument-server validate config path/to/config.yaml

# Validate an API definition
instrument-server validate api path/to/api.yaml
```

### Common Validation Errors

#### Error: "name must match pattern ^[A-Z][A-Z0-9_]*$"

**Problem**:  Instrument name uses lowercase or special characters.

**Solution**: Use uppercase letters, numbers, and underscores only.  Must start with a letter.

**Bad**: `dmm1`, `DMM-1`, `1DMM`  
**Good**: `DMM1`, `DMM_PRIMARY`, `SCOPE_A`

#### Error: "Unknown connection type"

**Problem**: `connection.type` is not `VISA` or `Custom`.

**Solution**: Use exactly `VISA` or `Custom` (case-sensitive).

#### Error: "io_config missing required port"

**Problem**: An IO port with role `input`, `output`, or `inout` is defined in the API but not configured.

**Solution**: Add the missing IO port to `io_config` in your configuration file.

#### Error: "Command template missing parameter"

**Problem**: A parameter is referenced in `{braces}` in the template but not defined in `parameters`.

**Solution**: Add the parameter to the `parameters` list.

---

## Best Practices

### Configuration Files

1. **Use descriptive names**:  `DMM_GATE_VOLTAGE` is better than `DMM1`
2. **Group related instruments**: Use prefixes like `DAC_`, `SCOPE_`, etc.
3. **Document with comments**:  YAML supports `#` comments
4. **Keep connection details separate**: Consider environment variables for addresses
5. **Validate before use**: Always run validation before starting instruments

### API Definitions

1. **Be explicit**:  Provide descriptions for all commands and IO ports
2. **Use consistent naming**: Follow SCPI conventions if applicable
3. **Define units**: Always specify physical units for measurements
4. **Group related IOs**: Use channel groups for multi-channel instruments
5. **Test incrementally**: Start with basic commands (IDN, RESET) before adding complex ones

### Example Directory Structure

```
my-lab-setup/
├── configs/
│   ├── dmm1.yaml              # Primary DMM
│   ├── dmm2.yaml              # Secondary DMM
│   ├── scope_left.yaml        # Left oscilloscope
│   └── scope_right.yaml       # Right oscilloscope
├── apis/
│   ├── agilent_34401a.yaml   # DMM API
│   └── keysight_scope. yaml   # Oscilloscope API
└── scripts/
    ├── iv_curve. lua
    ├── stability_diagram.lua
    └── calibration.lua
```

---

## See Also

- [CLI Usage Guide](CLI_USAGE.md) - How to use the command-line interface
- [Plugin Development Guide](PLUGIN_DEVELOPMENT.md) - Creating custom instrument drivers
- [Architecture Documentation](ARCHITECTURE.md) - System design and internals
- [Example Configurations](../examples/instrument-configurations/) - Working configuration examples
- [Example APIs](../examples/instrument-apis/) - Working API definition examples
