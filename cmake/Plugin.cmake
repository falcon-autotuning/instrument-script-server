# Helper function for users to create instrument plugins

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
