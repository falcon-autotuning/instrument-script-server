cmake_minimum_required(VERSION 3.20)

project(
  VISAPlugin
  VERSION 1.0.0
  LANGUAGES C
)

find_package(instrument-plugin-api CONFIG REQUIRED)
find_package(cJSON CONFIG REQUIRED)

set(VISA_PATH_HINTS)

# Environment variables (user-installed VISA)
if(DEFINED ENV{VISA_ROOT})
  list(APPEND VISA_PATH_HINTS $ENV{VISA_ROOT})
endif()

if(DEFINED ENV{NI_VISA_ROOT})
  list(APPEND VISA_PATH_HINTS $ENV{NI_VISA_ROOT})
endif()

# Default install locations
if(WIN32)
  list(APPEND VISA_PATH_HINTS
    "C:/Program Files/IVI Foundation/VISA/Win64"
    "C:/Program Files/IVI Foundation/VISA/WinNT"
    "C:/Program Files (x86)/IVI Foundation/VISA/WinNT"
  )
else()
  list(APPEND VISA_PATH_HINTS
    /usr
    /usr/local
    /opt/ni-visa
    /opt/visa
  )
endif()

find_path(VISA_INCLUDE_DIR
  NAMES visa.h
  HINTS ${VISA_PATH_HINTS}
  PATH_SUFFIXES include
)

find_library(VISA_LIBRARY
  NAMES visa visa64 nivisa visa32
  HINTS ${VISA_PATH_HINTS}
  PATH_SUFFIXES lib lib64 lib/x64 lib/msc
)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(VISA
  REQUIRED_VARS VISA_LIBRARY VISA_INCLUDE_DIR
)

if(NOT VISA_FOUND)
  message(WARNING "NI-VISA not found. VISA plugin will not be built.")
  message(WARNING "Install NI-VISA: https://www.ni.com/en-us/support/downloads/drivers/download.ni-visa.html")
  return()
endif()

add_library(VISA::VISA UNKNOWN IMPORTED)

set_target_properties(VISA::VISA PROPERTIES
  IMPORTED_LOCATION "${VISA_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${VISA_INCLUDE_DIR}"
)

message(STATUS "Found VISA include: ${VISA_INCLUDE_DIR}")
message(STATUS "Found VISA library: ${VISA_LIBRARY}")

add_instrument_plugin(visa_plugin
  SOURCES
    visa_plugin.c
  LINK_LIBRARIES
    VISA::VISA
    cjson
  INCLUDE_DIRS
    ${VISA_INCLUDE_DIR}
)

install(TARGETS visa_plugin
  LIBRARY DESTINATION lib/instrument-plugins
  RUNTIME DESTINATION bin/instrument-plugins
)
