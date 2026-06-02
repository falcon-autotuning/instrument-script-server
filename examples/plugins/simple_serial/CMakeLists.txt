cmake_minimum_required(VERSION 3.20)
project(
  SimpleSerialPlugin
  VERSION 1.0.0
  LANGUAGES C)

find_package(instrument-plugin-api CONFIG REQUIRED)

add_instrument_plugin(simple_serial_plugin
  SOURCES simple_serial.c
)
install(TARGETS simple_serial_plugin LIBRARY DESTINATION lib/instrument-plugins)
