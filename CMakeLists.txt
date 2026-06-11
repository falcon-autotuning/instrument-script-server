cmake_minimum_required(VERSION 3.20)
project(
  instrument-script-server
  VERSION 1.2.0
  LANGUAGES C CXX)

# Git version detection
find_package(Git QUIET)
set(INSTSERVER_VERSION "${PROJECT_VERSION}")
set(INSTSERVER_GIT_COMMIT "unknown")
set(INSTSERVER_GIT_TAG "")

if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE INSTSERVER_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_COMMIT_RESULT)

  if(NOT GIT_COMMIT_RESULT EQUAL 0)
    set(INSTSERVER_GIT_COMMIT "unknown")
  endif()

  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE INSTSERVER_GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE GIT_TAG_RESULT)

  # If we're not on a tagged commit, check if we have tags at all
  if(NOT GIT_TAG_RESULT EQUAL 0)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0 HEAD
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE INSTSERVER_GIT_TAG
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE GIT_TAG_ABBREV_RESULT)

    if(NOT GIT_TAG_ABBREV_RESULT EQUAL 0)
      set(INSTSERVER_GIT_TAG "")
    endif()
  endif()
endif()

message(STATUS "Version: ${INSTSERVER_VERSION}")
if(NOT INSTSERVER_GIT_COMMIT STREQUAL "unknown")
  message(STATUS "Git commit: ${INSTSERVER_GIT_COMMIT}")
endif()
if(INSTSERVER_GIT_TAG)
  message(STATUS "Git tag: ${INSTSERVER_GIT_TAG}")
else()
  message(STATUS "Git tag: (none found)")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build Options
option(BUILD_TESTS "Build tests" OFF)

# Performance options
option(USE_CCACHE "Enable ccache or sccache if available" ON)
option(ENABLE_PCH
       "Enable target_precompile_headers to speed up builds (opt-in)" ON)

# Windows-specific definitions
if(WIN32)
  add_compile_definitions(NOMINMAX)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Try to auto-detect a compiler launcher (prefer sccache if present)
if(USE_CCACHE)
  find_program(SCCACHE_PROGRAM sccache)
  if(SCCACHE_PROGRAM)
    message(STATUS "Using sccache: ${SCCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER ${SCCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${SCCACHE_PROGRAM})
  else()
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
      message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
      set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
      set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    else()
      message(
        STATUS "No ccache/sccache found (fine, builds will just be normal).")
    endif()
  endif()
endif()

# Find dependencies
find_package(Threads       REQUIRED)
find_package(Lua           REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp      CONFIG REQUIRED)
find_package(sol2          CONFIG REQUIRED)
find_package(instrument-data CONFIG REQUIRED)
find_package(instrument-log CONFIG REQUIRED)
find_package(instrument-call-stack CONFIG REQUIRED)

# Platform-specific settings
if(UNIX AND NOT APPLE)
  set(PLATFORM_LIBS rt pthread dl)
elseif(APPLE)
  set(PLATFORM_LIBS pthread dl)
elseif(WIN32)
  # Link Winsock on Windows
  set(PLATFORM_LIBS Ws2_32)
endif()

configure_file(${CMAKE_SOURCE_DIR}/src/embedded_schemas.cpp.in
               ${CMAKE_BINARY_DIR}/embedded_schemas.cpp @ONLY)

# Core library
add_library(
  instrument-script-server-core SHARED
  src/SchemaValidator.cpp
  src/version.cpp
  src/SerializedCommand.cpp
  src/ipc/SharedQueue.cpp
  src/ipc/ProcessManager.cpp
  src/ipc/DataBufferManager.cpp
  src/plugin/PluginLoader.cpp
  src/plugin/PluginRegistry.cpp
  src/server/InstrumentRegistry.cpp
  src/server/InstrumentWorkerProxy.cpp
  src/server/RuntimeContext.cpp
  src/server/SyncCoordinator.cpp
  src/server/ApiRefResolver.cpp
  src/server/ServerDaemon.cpp
  src/server/HttpRpcServer.cpp
  src/server/CommandHandlers.cpp
  src/server/JobManager.cpp
  ${CMAKE_BINARY_DIR}/embedded_schemas.cpp)

set_target_properties(instrument-script-server-core
                      PROPERTIES POSITION_INDEPENDENT_CODE ON)

# CRITICAL FOR WINDOWS: Define export symbol for DLL This matches the symbol
# checked in include/instrument-script-server/export.h
target_compile_definitions(instrument-script-server-core
                           PRIVATE instrument_server_core_EXPORTS)

# Add version information
target_compile_definitions(
  instrument-script-server-core
  PRIVATE
    INSTSERVER_VERSION="${INSTSERVER_VERSION}"
    INSTSERVER_GIT_COMMIT="${INSTSERVER_GIT_COMMIT}"
    INSTSERVER_GIT_TAG="${INSTSERVER_GIT_TAG}")

target_link_libraries(
  instrument-script-server-core
  PUBLIC
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    ${PLATFORM_LIBS}
    ${LUA_LIBRARIES}
    instrument-log::instrument-log
    instrument-data::instrument-data
    instrument-call-stack::instrument-call-stack
    instrument-call-stack::instrument-call-stack-lua
)

# Per-target include directories (avoid global include_directories)
target_include_directories(
  instrument-script-server-core
  PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
         $<$<BOOL:${LUA_INCLUDE_DIR}>:$<BUILD_INTERFACE:${LUA_INCLUDE_DIR}>>
         $<$<BOOL:${SOL2_INCLUDE_DIR}>:$<BUILD_INTERFACE:${SOL2_INCLUDE_DIR}>>)

# Optional precompiled headers
if(ENABLE_PCH)
  # Prefer a project-provided PCH header, fallback to common std headers.
  if(EXISTS "${CMAKE_SOURCE_DIR}/include/instrument-script-server/pch.h")
    message(STATUS "Using project PCH: include/instrument-script-server/pch.h")
    target_precompile_headers(
      instrument-script-server-core PRIVATE
      "${CMAKE_SOURCE_DIR}/include/instrument-script-server/pch.h")
  else()
    message(STATUS "No project PCH found; using common system headers as PCH")
    target_precompile_headers(
      instrument-script-server-core
      PRIVATE
      <vector>
      <string>
      <memory>
      <map>
      <set>)
  endif()
endif()

add_executable(instrument-script-server src/server/instrument_server_main.cpp)
add_executable(instrument-worker src/worker_main.cpp)
add_executable(validate-instrument-api src/instrument_api_validator_main.cpp)
add_executable(validate-instrument-config
                 src/instrument_config_validator_main.cpp)
add_executable(validate-quantum-dot-config
                 src/quantum_dot_config_validator_main.cpp)
add_executable(template-expander src/template_expander.cpp)
add_executable(generate-instrument-config
                 src/instrument_configuration_generator.cpp)
foreach(target
        instrument-script-server
        instrument-worker
        validate-instrument-api
        validate-instrument-config
        validate-quantum-dot-config
        template-expander
        generate-instrument-config
)
  target_link_libraries(${target} PRIVATE instrument-script-server-core)
endforeach()

# Tests
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS instrument-script-server-core
  EXPORT instrument-script-server-targets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(TARGETS
    instrument-script-server
    instrument-worker
    validate-instrument-api
    validate-instrument-config
    validate-quantum-dot-config
    template-expander
    generate-instrument-config
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
  DIRECTORY include/instrument-script-server
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING
    PATTERN "*.hpp"
    PATTERN "*.h"
)


install(
  DIRECTORY schemas/
  DESTINATION share/instrument-script-server/schemas
  FILES_MATCHING
  PATTERN "*.json")

install(
  DIRECTORY docs/
  DESTINATION share/doc/instrument-script-server
  FILES_MATCHING
  PATTERN "*.md")

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/instrument-script-server-config-version.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

install(EXPORT instrument-script-server-targets
  FILE      instrument-script-server-targets.cmake
  NAMESPACE instrument-script-server::
  DESTINATION ${CMAKE_INSTALL_DATADIR}/instrument-script-server
)

configure_package_config_file(
  cmake/instrument-script-server-config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/instrument-script-server-config.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_DATADIR}/instrument-script-server
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/instrument-script-server-config.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/instrument-script-server-config-version.cmake"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/instrument-script-server
)
