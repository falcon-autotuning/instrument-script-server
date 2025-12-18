cmake_minimum_required(VERSION 3.20)
project(
  SimpleSerialPlugin
  VERSION 1.0.0
  LANGUAGES C)

# Check if building as part of main project or standalone
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  # Standalone build - find installed InstrumentServer
  find_package(InstrumentServer REQUIRED)

  # Use the helper function
  add_instrument_plugin(simple_serial_plugin SOURCES simple_serial.c)
else()
  # Building as part of main project - use local headers
  add_library(simple_serial_plugin MODULE simple_serial.c)

  set_target_properties(simple_serial_plugin
                        PROPERTIES PREFIX "" POSITION_INDEPENDENT_CODE ON)

  target_include_directories(simple_serial_plugin
                             PRIVATE ${CMAKE_SOURCE_DIR}/include)

  # Don't link to core library - plugins should be standalone They only need the
  # header for the interface
endif()

# Install plugin
install(TARGETS simple_serial_plugin LIBRARY DESTINATION lib/instrument-plugins)
