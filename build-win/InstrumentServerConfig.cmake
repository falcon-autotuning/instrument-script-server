
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

# Include the targets file
include("${CMAKE_CURRENT_LIST_DIR}/InstrumentServerTargets.cmake")

# Set include directories
set(InstrumentServer_INCLUDE_DIRS "${PACKAGE_PREFIX_DIR}/include")

# Helper function for external plugins to easily create plugins
function(add_instrument_plugin TARGET_NAME)
  set(options "")
  set(oneValueArgs "")
  set(multiValueArgs SOURCES LINK_LIBRARIES INCLUDE_DIRS)

  cmake_parse_arguments(PLUGIN "${options}" "${oneValueArgs}"
                        "${multiValueArgs}" ${ARGN})

  if(NOT PLUGIN_SOURCES)
    message(FATAL_ERROR "add_instrument_plugin:  SOURCES argument is required")
  endif()

  # Create MODULE library (for dlopen/LoadLibrary)
  add_library(${TARGET_NAME} MODULE ${PLUGIN_SOURCES})

  # Set properties for plugin
  set_target_properties(
    ${TARGET_NAME}
    PROPERTIES PREFIX "" # No 'lib' prefix
               POSITION_INDEPENDENT_CODE ON
               C_VISIBILITY_PRESET default # Export all symbols
               CXX_VISIBILITY_PRESET default)

  # Platform-specific suffix
  if(WIN32)
    set_target_properties(${TARGET_NAME} PROPERTIES SUFFIX ".dll")
  elseif(APPLE)
    set_target_properties(${TARGET_NAME} PROPERTIES SUFFIX ".dylib")
  else()
    set_target_properties(${TARGET_NAME} PROPERTIES SUFFIX ".so")
  endif()

  # Include InstrumentServer headers
  target_include_directories(
    ${TARGET_NAME} PRIVATE ${InstrumentServer_INCLUDE_DIRS}
                           ${PLUGIN_INCLUDE_DIRS})

  # Link to core library (provides plugin interface)
  target_link_libraries(${TARGET_NAME}
                        PRIVATE InstrumentServer::instrument-server-core)

  # Link additional libraries
  if(PLUGIN_LINK_LIBRARIES)
    target_link_libraries(${TARGET_NAME} PRIVATE ${PLUGIN_LINK_LIBRARIES})
  endif()

  message(STATUS "Configured instrument plugin:  ${TARGET_NAME}")
endfunction()

check_required_components(InstrumentServer)
