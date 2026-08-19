cmake_minimum_required(VERSION 3.20)
project(
  instrument-script-server
  VERSION 2.0.12
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

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build Options
option(BUILD_TESTS "Build tests" OFF)
option(BUILD_C_CLIENT "Build C99 gRPC client wrapper" OFF)
option(BUILD_CLI "Build ISS CLI" OFF)
# Performance options
option(USE_CCACHE "Enable ccache or sccache if available" ON)

if(BUILD_CLI AND NOT BUILD_C_CLIENT)
  message(STATUS "Enabling BUILD_C_CLIENT because BUILD_CLI is ON")
  set(BUILD_C_CLIENT ON CACHE BOOL "" FORCE)
endif()

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

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(ENABLE_ASAN)
  message(STATUS "ASAN ENABLED")

  set(SAN_COMPILE_FLAGS
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
    -g
    -O1
  )

  set(SAN_LINK_FLAGS
    -fsanitize=address,undefined
    -Wl,--export-dynamic
  )

  add_compile_options(${SAN_COMPILE_FLAGS})
  add_link_options(${SAN_LINK_FLAGS})
endif()

# Find dependencies
find_package(Threads       REQUIRED)
find_package(Lua           REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp      CONFIG REQUIRED)
find_package(sol2          CONFIG REQUIRED)
find_package(gRPC          CONFIG REQUIRED)
find_package(cxxopts       CONFIG REQUIRED)
find_package(instrument-data CONFIG REQUIRED)
find_package(instrument-log CONFIG REQUIRED)
find_package(instrument-call-stack CONFIG REQUIRED)
find_package(instrument-plugin-api CONFIG REQUIRED)

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
add_subdirectory(proto)

# Core library
add_library(
  instrument-script-server-core
    SHARED
      src/SchemaValidator.cpp
      src/version.cpp
      src/ipc/SharedQueue.cpp
      src/ipc/ProcessManager.cpp
      src/core/PluginLoader.cpp
      src/core/InstrumentCommand.cpp
      src/core/ParsingTools.cpp
      src/core/GlobalConfig.cpp
      ${CMAKE_BINARY_DIR}/embedded_schemas.cpp
)

set_target_properties(
  instrument-script-server-core
    PROPERTIES
      POSITION_INDEPENDENT_CODE ON
)

# CRITICAL FOR WINDOWS: Define export symbol for DLL This matches the symbol
# checked in include/instrument-script-server/export.h
target_compile_definitions(instrument-script-server-core
                           PRIVATE instrument_server_core_EXPORTS)

# Add version information
target_compile_definitions(
  instrument-script-server-core
  PUBLIC
    INSTSERVER_VERSION="${INSTSERVER_VERSION}"
    INSTSERVER_GIT_COMMIT="${INSTSERVER_GIT_COMMIT}"
    INSTSERVER_GIT_TAG="${INSTSERVER_GIT_TAG}")

target_link_libraries(
  instrument-script-server-core
  PUBLIC
    yaml-cpp::yaml-cpp
    instrument-log::instrument-log
)

target_include_directories(
  instrument-script-server-core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

#Daemon library
add_library(
  internal-daemon
    STATIC
      src/daemon/DataBufferManager.cpp
      src/daemon/PluginRegistry.cpp
      src/daemon/InstrumentRegistry.cpp
      src/daemon/InstrumentWorkerProxy.cpp
      src/daemon/RuntimeContext.cpp
      src/daemon/SyncCoordinator.cpp
      src/daemon/ApiRefResolver.cpp
      src/daemon/ServerDaemon.cpp
      src/daemon/GrpcServer.cpp
      src/daemon/CommandHandlers.cpp
      src/daemon/JobManager.cpp
)

target_link_libraries(internal-daemon
  PRIVATE
    instrument-script-server-core
  PUBLIC
    nlohmann_json::nlohmann_json
    ${PLATFORM_LIBS}
    ${LUA_LIBRARIES}
    instrument-data::instrument-data
    instrument-call-stack::instrument-call-stack
    instrument-call-stack::instrument-call-stack-lua
    instserver-proto
)

target_include_directories(
  internal-daemon
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    $<$<BOOL:${LUA_INCLUDE_DIR}>:$<BUILD_INTERFACE:${LUA_INCLUDE_DIR}>>
    $<$<BOOL:${SOL2_INCLUDE_DIR}>:$<BUILD_INTERFACE:${SOL2_INCLUDE_DIR}>>
)

add_executable(instrument-script-server-daemon src/daemon/daemon_main.cpp)
add_executable(instrument-worker src/worker_main.cpp)
add_executable(validate-instrument-api src/instrument_api_validator_main.cpp)
add_executable(validate-instrument-config
                 src/instrument_config_validator_main.cpp)
add_executable(validate-quantum-dot-config
                 src/quantum_dot_config_validator_main.cpp)
add_executable(template-expander src/template_expander.cpp)
add_executable(generate-instrument-config
                 src/instrument_configuration_generator.cpp)
if(BUILD_CLI)
  add_executable(instrument-script-server src/instrument_server_main.cpp)
endif()
target_link_libraries(instrument-script-server-daemon
  PRIVATE
    internal-daemon
)
target_link_libraries(instrument-worker
  PRIVATE
    instrument-plugin-api::instrument-plugin-api-host
    instrument-data::instrument-data
)
foreach(target
        instrument-script-server-daemon
        validate-instrument-api
        validate-instrument-config
        validate-quantum-dot-config
        template-expander
        generate-instrument-config
        instrument-worker
)
  target_link_libraries(${target} PRIVATE instrument-script-server-core)
endforeach()
if(BUILD_C_CLIENT)
  add_library(instrument-script-server-client STATIC
    src/client/instrument-server-client.cpp
  )

  target_link_libraries(instrument-script-server-client
    PUBLIC
      instserver-proto
  )

  target_include_directories(instrument-script-server-client
    BEFORE PRIVATE
      ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
    PRIVATE
      ${CMAKE_BINARY_DIR}/proto
  )
endif()
if(BUILD_CLI)
  target_link_libraries(instrument-script-server
    PRIVATE
      instrument-script-server-client
      instrument-data::instrument-data
  )
  target_compile_definitions(
    instrument-script-server
    PRIVATE
      INSTSERVER_VERSION="${INSTSERVER_VERSION}"
      INSTSERVER_GIT_COMMIT="${INSTSERVER_GIT_COMMIT}"
      INSTSERVER_GIT_TAG="${INSTSERVER_GIT_TAG}")

  target_include_directories(instrument-script-server
    PRIVATE
      $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
  )
endif()

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
if(BUILD_C_CLIENT)
  install(TARGETS instrument-script-server-client
    EXPORT instrument-script-server-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )
endif()

install(TARGETS
    instrument-script-server-daemon
    instrument-worker
    validate-instrument-api
    validate-instrument-config
    validate-quantum-dot-config
    template-expander
    generate-instrument-config
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(BUILD_CLI)
  install(TARGETS instrument-script-server
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()

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
