cmake_minimum_required(VERSION 3.20)
project(
  SimpleSerialPlugin
  VERSION 1.0.0
  LANGUAGES C)

# Check if building as part of main project or standalone
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  # Standalone build - find installed InstrumentServer
  find_package(InstrumentServer REQUIRED)

  # Use the helper function (if provided by upstream)
  add_instrument_plugin(simple_serial_plugin SOURCES simple_serial.c)
else()
  add_library(simple_serial_plugin SHARED simple_serial.c)
  target_compile_definitions(simple_serial_plugin
                             PRIVATE INSTRUMENT_PLUGIN_EXPORTS)

  # Prefer per-target include path (relative to top-level CMake)
  target_include_directories(simple_serial_plugin
                             PRIVATE ${CMAKE_SOURCE_DIR}/include)

  # Add other target properties as needed
  set_target_properties(simple_serial_plugin
                        PROPERTIES PREFIX "" POSITION_INDEPENDENT_CODE ON)

  # Plugins should be standalone; they only need the headers for the interface
endif()

# Install plugin
install(TARGETS simple_serial_plugin LIBRARY DESTINATION lib/instrument-plugins)
