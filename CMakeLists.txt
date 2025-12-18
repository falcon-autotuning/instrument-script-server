cmake_minimum_required(VERSION 3.20)
project(
  InstrumentScriptSchema
  VERSION 1.0.0
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Options
option(BUILD_TESTS "Build tests" ON)
option(BUILD_EXAMPLES "Build example plugins" ON)
option(BUILD_TOOLS "Build command-line tools" ON)

# Find dependencies
find_package(Boost REQUIRED COMPONENTS filesystem)
find_package(nlohmann_json REQUIRED)
find_package(yaml-cpp REQUIRED CONFIG)
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

# Embed JSON schemas into C++ source
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/schemas/instrument_api.schema.json"
     INSTRUMENT_API_SCHEMA)
file(READ
     "${CMAKE_CURRENT_SOURCE_DIR}/schemas/instrument_configuration.schema.json"
     INSTRUMENT_CONFIGURATION_SCHEMA)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/embedded_schemas.cpp.in"
               "${CMAKE_CURRENT_BINARY_DIR}/embedded_schemas.cpp" @ONLY)

add_library(
  instrument_server_lib SHARED
  src/ipc/SharedQueue.cpp
  src/ipc/ProcessManager.cpp
  src/ipc/WorkerProtocol.cpp
  src/plugin/PluginLoader.cpp
  src/plugin/PluginRegistry.cpp
  src/server/InstrumentWorkerProxy.cpp
  src/server/InstrumentRegistry.cpp
  src/server/RuntimeContext.cpp
  src/SchemaValidator.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/embedded_schemas.cpp)
target_include_directories(
  instrument_server_lib
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include> ${LUA_INCLUDE_DIR})
target_link_libraries(
  instrument_server_lib
  PUBLIC Boost::filesystem
         nlohmann_json::nlohmann_json
         yaml-cpp::yaml-cpp
         fmt::fmt
         spdlog::spdlog
         sol2::sol2
         ${LUA_LIBRARIES})
if(UNIX)
  target_link_libraries(instrument_server_lib PUBLIC pthread dl rt)
endif()
add_executable(instrument-worker src/workers/generic_worker_main.cpp)
target_link_libraries(instrument-worker PRIVATE instrument_server_lib)
add_executable(instrument-server src/server/instrument_server_main.cpp)
target_link_libraries(instrument-server PRIVATE instrument_server_lib)
if(HAVE_VISA)
  add_library(visa_builtin_plugin SHARED src/workers/visa_builtin.cpp)
  target_link_libraries(visa_builtin_plugin PRIVATE instrument_server_lib
                                                    ${VISA_LIBRARY})
  set_target_properties(visa_builtin_plugin PROPERTIES PREFIX "" OUTPUT_NAME
                                                                 "visa_builtin")
endif()
if(BUILD_TOOLS)
  add_executable(instrument-setup src/tools/instrument_setup.cpp)
  target_link_libraries(instrument-setup PUBLIC yaml-cpp::yaml-cpp
                                                instrument_server_lib)

  add_executable(template_expander src/template_expander.cpp)
  target_link_libraries(template_expander PRIVATE yaml-cpp::yaml-cpp)

  add_executable(generate_instrument_configuration
                 src/instrument_configuration_generator.cpp)
  target_link_libraries(generate_instrument_configuration
                        PRIVATE yaml-cpp::yaml-cpp)

  add_executable(validate_instrument_api src/instrument_api_validator_main.cpp)
  target_link_libraries(validate_instrument_api PRIVATE instrument_server_lib)

  add_executable(validate_quantum_dot_config
                 src/quantum_dot_config_validator_main.cpp)
  target_link_libraries(validate_quantum_dot_config
                        PRIVATE instrument_server_lib)

  add_executable(validate_instrument_config
                 src/instrument_config_validator_main.cpp)
  target_link_libraries(validate_instrument_config
                        PRIVATE instrument_server_lib)
endif()

# Install
install(
  TARGETS instrument_server_lib instrument-worker instrument-server
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

install(DIRECTORY include/instrument-server DESTINATION include)

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
  NAMESPACE instserver::
  DESTINATION lib/cmake/InstrumentServer)

# Create plugin directory
install(DIRECTORY DESTINATION lib/instrument-plugins)
# Tests
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
# Examples
if(BUILD_EXAMPLES)
  set(CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}" ${CMAKE_PREFIX_PATH})
  add_subdirectory(examples/plugins/simple_serial)
endif()

# Package
set(CPACK_PACKAGE_NAME "InstrumentServer")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "Falcon Autotuning")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Modular instrument control server")
set(CPACK_GENERATOR "TGZ;DEB;RPM")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libboost-all-dev, libyaml-cpp-dev, libfmt-dev, libspdlog-dev, lua5.4")
include(CPack)
