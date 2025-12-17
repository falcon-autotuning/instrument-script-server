
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was InstrumentServerConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

set(InstrumentServer_VERSION 1.0.0)

# Include directories
set(InstrumentServer_INCLUDE_DIRS "/usr/local/include")

# Libraries
set(InstrumentServer_LIBRARIES
    "/usr/local/lib/libinstrument-server.so")

function(add_instrument_plugin TARGET_NAME)
  set(options "")
  set(oneValueArgs "")
  set(multiValueArgs SOURCES LINK_LIBRARIES INCLUDE_DIRS)

  cmake_parse_arguments(PLUGIN "${options}" "${oneValueArgs}"
                        "${multiValueArgs}" ${ARGN})

  # Create shared library
  add_library(${TARGET_NAME} SHARED ${PLUGIN_SOURCES})

  # Set properties for plugin
  set_target_properties(
    ${TARGET_NAME}
    PROPERTIES PREFIX "" # No 'lib' prefix on Unix
               SUFFIX ".so" # Force . so even on Windows for consistency (or use
                            # platform-specific)
               C_VISIBILITY_PRESET hidden
               CXX_VISIBILITY_PRESET hidden
               POSITION_INDEPENDENT_CODE ON)

  # Include InstrumentServer headers
  target_include_directories(
    ${TARGET_NAME} PRIVATE ${InstrumentServer_INCLUDE_DIRS}
                           ${PLUGIN_INCLUDE_DIRS})

  # Link libraries
  if(PLUGIN_LINK_LIBRARIES)
    target_link_libraries(${TARGET_NAME} PRIVATE ${PLUGIN_LINK_LIBRARIES})
  endif()

  # Export plugin symbols
  if(WIN32)
    target_compile_definitions(${TARGET_NAME}
                               PRIVATE "PLUGIN_EXPORT=__declspec(dllexport)")
  else()
    target_compile_definitions(
      ${TARGET_NAME}
      PRIVATE "PLUGIN_EXPORT=__attribute__((visibility(\"default\")))")
  endif()

  message(STATUS "Configured instrument plugin: ${TARGET_NAME}")
endfunction()

check_required_components(InstrumentServer)
