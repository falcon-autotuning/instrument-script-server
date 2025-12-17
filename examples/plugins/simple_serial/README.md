# Simple Serial Plugin Example

<!--toc:start-->
- [Simple Serial Plugin Example](#simple-serial-plugin-example)
  - [Building](#building)
  - [Installing](#installing)
  - [Usage](#usage)
<!--toc:end-->

This is a minimal example of an instrument plugin for the Instrument Script Server.

## Building

```bash
mkdir build
cd build
cmake ..  -DCMAKE_PREFIX_PATH=/path/to/instrument-server-install
cmake --build . 
```

## Installing

```bash
sudo cmake --install .
```

Or manually copy the plugin:

```bash
sudo cp simple_serial_plugin. so /usr/local/lib/instrument-plugins/
```

## Usage

Create an instrument config:

```YAML
name: MySerialDevice
api_ref: path/to/api.yaml
connection:
  type: SimpleSerial
  device: "/dev/ttyUSB0"
  plugin:  "/usr/local/lib/instrument-plugins/simple_serial_plugin. so"
```

The plugin will be automatically loaded when you run the instrument server.
