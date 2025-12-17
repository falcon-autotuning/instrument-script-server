cmake_minimum_required(VERSION 3.20)
project(SimpleSerialPlugin VERSION 1.0.0)

# Find InstrumentServer package
find_package(InstrumentServer REQUIRED)

# Create plugin
add_instrument_plugin(simple_serial_plugin SOURCES simple_serial.c)

# Install plugin
install(TARGETS simple_serial_plugin LIBRARY DESTINATION lib/instrument-plugins)
