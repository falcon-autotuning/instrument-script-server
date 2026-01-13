cmake_minimum_required(VERSION 3.20)
project(
  instrument-server
  VERSION 0.1.0
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(BUILD_TESTS "Build tests" ON)
option(BUILD_EXAMPLES "Build example plugins" ON)
option(BUILD_TOOLS "Build command-line tools" ON)

# Find dependencies
find_package(Threads REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)

message(STATUS "CMAKE_SYSROOT=${CMAKE_SYSROOT}")
message(STATUS "CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}")
message(STATUS "CMAKE_FIND_ROOT_PATH=${CMAKE_FIND_ROOT_PATH}")

# Lua (prefer LuaJIT if available)
find_package(LuaJIT QUIET)
if(LuaJIT_FOUND)
  set(LUA_LIBRARIES ${LUAJIT_LIBRARIES})
  set(LUA_INCLUDE_DIR ${LUAJIT_INCLUDE_DIR})
else()
  if(CMAKE_CROSSCOMPILING)
    # When cross-compiling, avoid FindLua header probing (can hang). A sysroot
    # will usually have include and lib under CMAKE_SYSROOT.
    message(
      STATUS "Cross-compiling: using sysroot-provided Lua (no FindLua probing)")
    set(LUA_INCLUDE_DIR "${CMAKE_SYSROOT}/include")
    # Prefer static import library; adjust name if your sysroot provides a
    # different file:
    set(LUA_LIBRARY "${CMAKE_SYSROOT}/lib/liblua.a")
    set(LUA_LIBRARIES "${LUA_LIBRARY}")
  else()
    find_package(Lua REQUIRED)
  endif()
endif()

# sol2 (header-only)
if(CMAKE_CROSSCOMPILING)
  # When cross-compiling, prefer sysroot paths to avoid long probing/regex
  # parsing.
  find_path(
    SOL2_INCLUDE_DIR sol/sol.hpp
    PATHS ${CMAKE_SYSROOT}/include ${CMAKE_SYSROOT}/usr/include
    NO_DEFAULT_PATH)
  if(NOT SOL2_INCLUDE_DIR)
    message(WARNING "sol2 header not found in sysroot (${CMAKE_SYSROOT}). \
Please install sol2 for the target toolchain or \
pass -DSOL2_INCLUDE_DIR=/path/to/sol/includes")
    # provide a sensible fallback so build can continue if headers are
    # colocated:
    set(SOL2_INCLUDE_DIR "${CMAKE_SYSROOT}/include")
  endif()
else()
  # native build: use standard find_path (and require it)
  find_path(SOL2_INCLUDE_DIR sol/sol.hpp REQUIRED)
endif()

message(STATUS "Using sol2 includes: ${SOL2_INCLUDE_DIR}")
include_directories(${SOL2_INCLUDE_DIR})

# VISA (optional)
find_library(
  VISA_LIBRARY
  NAMES visa visa64
  PATHS /usr/local/vxipnp/linux64/lib)
if(VISA_LIBRARY)
  message(STATUS "Found VISA library: ${VISA_LIBRARY}")
  set(HAVE_VISA ON)
else()
  message(STATUS "VISA not found, VISA built-in plugin will be disabled")
  set(HAVE_VISA OFF)
endif()

# Platform-specific settings
if(UNIX AND NOT APPLE)
  set(PLATFORM_LIBS rt pthread dl)
elseif(APPLE)
  set(PLATFORM_LIBS pthread dl)
elseif(WIN32)
  set(PLATFORM_LIBS "")
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include ${LUA_INCLUDE_DIR}
                    ${SOL2_INCLUDE_DIR})

configure_file(${CMAKE_SOURCE_DIR}/src/embedded_schemas.cpp.in
               ${CMAKE_BINARY_DIR}/embedded_schemas.cpp @ONLY)

# Core library
add_library(
  instrument-server-core SHARED
  src/SchemaValidator.cpp
  src/SerializedCommand.cpp
  src/ipc/SharedQueue.cpp
  src/ipc/ProcessManager.cpp
  src/ipc/DataBufferManager.cpp
  src/ipc/DataBufferManager_c_api.cpp
  src/plugin/PluginLoader.cpp
  src/plugin/PluginRegistry.cpp
  src/server/InstrumentRegistry.cpp
  src/server/InstrumentWorkerProxy.cpp
  src/server/RuntimeContext.cpp
  src/server/SyncCoordinator.cpp
  src/server/ServerDaemon.cpp
  ${CMAKE_BINARY_DIR}/embedded_schemas.cpp)

set_target_properties(instrument-server-core
                      PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(
  instrument-server-core PUBLIC spdlog::spdlog nlohmann_json::nlohmann_json
                                yaml-cpp ${LUA_LIBRARIES} ${PLATFORM_LIBS})

target_include_directories(
  instrument-server-core PUBLIC $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
                                $<INSTALL_INTERFACE:include>)

add_executable(instrument-server src/server/instrument_server_main.cpp)
target_link_libraries(instrument-server PRIVATE instrument-server-core)

# Worker executable
add_executable(instrument-worker src/workers/generic_worker_main.cpp)
target_link_libraries(instrument-worker PRIVATE instrument-server-core)

# VISA built-in plugin (optional)
if(HAVE_VISA)
  add_library(visa_builtin MODULE src/workers/visa_builtin.cpp)
  target_link_libraries(visa_builtin PRIVATE instrument-server-core
                                             ${VISA_LIBRARY})
  set_target_properties(visa_builtin PROPERTIES PREFIX "" OUTPUT_NAME
                                                          "visa_builtin")
endif()

# Tools
if(BUILD_TOOLS)
  # Validators
  add_executable(validate-instrument-api src/instrument_api_validator_main.cpp)
  target_link_libraries(validate-instrument-api PRIVATE instrument-server-core)

  add_executable(validate-instrument-config
                 src/instrument_config_validator_main.cpp)
  target_link_libraries(validate-instrument-config
                        PRIVATE instrument-server-core)

  add_executable(validate-quantum-dot-config
                 src/quantum_dot_config_validator_main.cpp)
  target_link_libraries(validate-quantum-dot-config
                        PRIVATE instrument-server-core)
  add_executable(template-expander src/template_expander.cpp)
  target_link_libraries(template-expander PRIVATE instrument-server-core)
  add_executable(generate-instrument-config
                 src/instrument_configuration_generator.cpp)
  target_link_libraries(generate-instrument-config
                        PRIVATE instrument-server-core)
endif()

# Example plugins (FIXED: only build if InstrumentServer is already installed OR
# during same build)
if(BUILD_EXAMPLES)
  # Build example plugins using local headers (not via find_package)
  add_subdirectory(examples/plugins/simple_serial)
endif()

# Build built-in plugins
add_subdirectory(plugins/visa)

# Tests
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Installation
install(TARGETS instrument-server instrument-worker RUNTIME DESTINATION bin)

if(BUILD_TOOLS)
  install(TARGETS validate-instrument-api validate-instrument-config
                  validate-quantum-dot-config template-expander
                  generate-instrument-config RUNTIME DESTINATION bin)
endif()

install(
  DIRECTORY include/
  DESTINATION include
  FILES_MATCHING
  PATTERN "*.h"
  PATTERN "*.hpp")

if(HAVE_VISA)
  install(TARGETS visa_builtin LIBRARY DESTINATION lib/instrument-plugins)
endif()

install(
  DIRECTORY schemas/
  DESTINATION share/instrument-server/schemas
  FILES_MATCHING
  PATTERN "*.json")

install(
  DIRECTORY docs/
  DESTINATION share/doc/instrument-server
  FILES_MATCHING
  PATTERN "*.md")

# Export targets for use by external plugins
install(
  TARGETS instrument-server-core
  EXPORT InstrumentServerTargets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  INCLUDES
  DESTINATION include)

install(
  EXPORT InstrumentServerTargets
  FILE InstrumentServerTargets.cmake
  NAMESPACE InstrumentServer::
  DESTINATION lib/cmake/InstrumentServer)

# Generate CMake config files
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/InstrumentServerConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstrumentServerConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/InstrumentServerConfig.cmake"
  INSTALL_DESTINATION lib/cmake/InstrumentServer
  PATH_VARS CMAKE_INSTALL_PREFIX)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/InstrumentServerConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/InstrumentServerConfigVersion.cmake"
        DESTINATION lib/cmake/InstrumentServer)
