cmake_minimum_required(VERSION 3.20)

project(
  VISAPlugin
  VERSION 1.0.0
  LANGUAGES C CXX)

# Find VISA library
find_library(
  VISA_LIBRARY
  NAMES visa visa64 nivisa
  PATHS /usr/lib /usr/local/lib /opt/ni-visa/lib
        "C:/Program Files/IVI Foundation/VISA/WinNT/lib/msc"
        "C:/Program Files (x86)/IVI Foundation/VISA/WinNT/lib/msc")

find_path(
  VISA_INCLUDE_DIR
  NAMES visa.h
  PATHS /usr/include /usr/local/include /opt/ni-visa/include
        "C:/Program Files/IVI Foundation/VISA/WinNT/include"
        "C:/Program Files (x86)/IVI Foundation/VISA/WinNT/include")

if(NOT VISA_LIBRARY OR NOT VISA_INCLUDE_DIR)
  message(WARNING "NI-VISA not found.  VISA plugin will not be built.")
  message(
    WARNING
      "Install NI-VISA from:  https://www.ni.com/en-us/support/downloads/drivers/download.ni-visa.html"
  )
  return()
endif()

message(STATUS "Found VISA library:  ${VISA_LIBRARY}")
message(STATUS "Found VISA headers: ${VISA_INCLUDE_DIR}")

# Find cJSON
find_package(PkgConfig)
if(PkgConfig_FOUND)
  pkg_check_modules(CJSON libcjson)
endif()

# Fallback:  try to find cJSON manually
if(NOT CJSON_FOUND)
  find_library(CJSON_LIBRARY NAMES cjson)
  find_path(CJSON_INCLUDE_DIR NAMES cjson/cJSON.h)

  if(CJSON_LIBRARY AND CJSON_INCLUDE_DIR)
    set(CJSON_LIBRARIES ${CJSON_LIBRARY})
    set(CJSON_INCLUDE_DIRS ${CJSON_INCLUDE_DIR})
    set(CJSON_FOUND TRUE)
  endif()
endif()

if(NOT CJSON_FOUND)
  message(WARNING "cJSON not found. VISA plugin will not be built.")
  message(
    WARNING
      "Install cJSON:  sudo pacman -S cjson (Arch) or sudo apt install libcjson-dev (Debian/Ubuntu)"
  )
  return()
endif()

message(STATUS "Found cJSON library: ${CJSON_LIBRARIES}")
message(STATUS "Found cJSON headers: ${CJSON_INCLUDE_DIRS}")

# Create the plugin
add_library(visa_plugin MODULE visa_plugin.c)

set_target_properties(
  visa_plugin
  PROPERTIES PREFIX ""
             POSITION_INDEPENDENT_CODE ON
             C_VISIBILITY_PRESET default)

target_include_directories(
  visa_plugin PRIVATE ${CMAKE_SOURCE_DIR}/include ${VISA_INCLUDE_DIR}
                      ${CJSON_INCLUDE_DIRS})

target_link_libraries(
  visa_plugin
  PRIVATE ${VISA_LIBRARY} ${CJSON_LIBRARIES}
          InstrumentServerCore # Link to core library for DataBufferManager
)

# Install to standard plugin directory
install(TARGETS visa_plugin LIBRARY DESTINATION lib/instrument-plugins)
