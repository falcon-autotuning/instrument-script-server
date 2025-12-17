cmake_minimum_required(VERSION 3.20)
project(
  InstrumentScriptSchema
  VERSION 1.0.0
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Options
option(BUILD_TESTS "Build tests" ON)
option(BUILD_EXAMPLES "Build example plugins" ON)
option(BUILD_TOOLS "Build command-line tools" ON)

# Find dependencies
find_package(Boost REQUIRED COMPONENTS system filesystem)
find_package(nlohmann_json REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
find_package(sol2 REQUIRED)
find_package(Lua REQUIRED)

# Optional:  VISA (for built-in VISA plugin)
find_library(
  VISA_LIBRARY
  NAMES visa visa32
  PATHS /usr/lib /usr/local/lib
        "C:/Program Files/IVI Foundation/VISA/Win64/Lib_x64/msc")
if(VISA_LIBRARY)
  set(HAVE_VISA TRUE)
  message(STATUS "Found VISA library: ${VISA_LIBRARY}")
else()
  set(HAVE_VISA FALSE)
  message(
    STATUS "VISA library not found - built-in VISA plugin will not be built")
endif()

# Main library
add_library(
  instrument_script SHARED
  # IPC
  src/ipc/SharedQueue.cpp
  src/ipc/ProcessManager.cpp
  src/ipc/WorkerProtocol.cpp
  # Plugin
  src/plugin/PluginLoader.cpp
  src/plugin/PluginRegistry.cpp
  # Server
  src/server/InstrumentWorkerProxy.cpp
  src/server/InstrumentRegistry.cpp
  src/server/RuntimeContext.cpp
  # Existing schema code
  src/schema_validator.
  cpp
  src/template_expander.cpp)

target_include_directories(
  instrument_script
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include> ${LUA_INCLUDE_DIR})

target_link_libraries(
  instrument_script
  PUBLIC Boost::system
         Boost::
         filesystem
         nlohmann_json::nlohmann_json
         yaml-cpp
         fmt::fmt
         spdlog::spdlog
         sol2::sol2
         ${LUA_LIBRARIES})

# Platform-specific libraries
if(UNIX)
  target_link_libraries(instrument_script PUBLIC pthread dl rt)
endif()

# Generic worker executable
add_executable(instrument-worker src/workers/generic_worker_main.cpp)
target_link_libraries(instrument-worker PRIVATE instrument_script)

# Server executable
add_executable(instrument-server src/server/instrument_server_main.cpp)
target_link_libraries(instrument-server PRIVATE instrument_script)

# Built-in VISA plugin
if(HAVE_VISA)
  add_library(visa_builtin_plugin SHARED src/workers/visa_builtin. cpp)
  target_link_libraries(visa_builtin_plugin PRIVATE instrument_script
                                                    ${VISA_LIBRARY})
  set_target_properties(visa_builtin_plugin PROPERTIES PREFIX "" OUTPUT_NAME
                                                                 "visa_builtin")
endif()

# Tools
if(BUILD_TOOLS)
  add_executable(instrument-setup src/tools/instrument_setup.cpp)
  target_link_libraries(instrument-setup PRIVATE instrument_script)
endif()

# Tests
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Examples
if(BUILD_EXAMPLES)
  add_subdirectory(examples/plugins/simple_serial)
endif()

# Install
install(
  TARGETS instrument_script instrument-worker instrument-server
  EXPORT InstrumentServerTargets
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
  RUNTIME DESTINATION bin)

if(HAVE_VISA)
  install(TARGETS visa_builtin_plugin
          LIBRARY DESTINATION lib/instrument-plugins)
endif()

if(BUILD_TOOLS)
  install(TARGETS instrument-setup RUNTIME DESTINATION bin)
endif()

install(DIRECTORY include/instrument_script DESTINATION include)

install(FILES cmake/Plugin.cmake DESTINATION lib/cmake/InstrumentServer)

# Export configuration
include(CMakePackageConfigHelpers)

configure_package_config_file(
  cmake/InstrumentServerConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/InstrumentServerConfig.cmake
  INSTALL_DESTINATION lib/cmake/InstrumentServer)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/InstrumentServerConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/InstrumentServerConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/InstrumentServerConfigVersion.cmake
        DESTINATION lib/cmake/InstrumentServer)

install(
  EXPORT InstrumentServerTargets
  FILE InstrumentServerTargets.cmake
  NAMESPACE InstrumentServer::
  DESTINATION lib/cmake/InstrumentServer)

# Create plugin directory
install(DIRECTORY DESTINATION lib/instrument-plugins)

# Package
set(CPACK_PACKAGE_NAME "InstrumentServer")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "Falcon Autotuning")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modular instrument control server")
set(CPACK_GENERATOR "TGZ;DEB;RPM")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libboost-all-dev, libyaml-cpp-dev, libfmt-dev, libspdlog-dev, lua5.4")
include(CPack)
