# Find Google Test
find_package(GTest REQUIRED)
include(GoogleTest)

# Test utilities library
add_library(test-utils STATIC test_utils/MockInstrument.cpp
                              test_utils/TestFixtures.cpp)

target_link_libraries(test-utils PUBLIC instrument-server-core GTest::gtest)

target_include_directories(test-utils
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/test_utils)

# Unit tests
add_executable(
  unit_tests
  unit/test_serialization.cpp
  unit/test_sync_coordinator.cpp
  unit/test_runtime_context.cpp
  unit/test_ipc_queue.cpp
  unit/test_server_daemon.cpp
  unit/test_schema_validator.cpp
  unit/test_plugin_loader.cpp
  unit/test_plugin_registry.cpp)

target_link_libraries(unit_tests PRIVATE instrument-server-core test-utils
                                         GTest::gtest GTest::gtest_main)

gtest_discover_tests(unit_tests)

# Integration tests
add_executable(
  integration_tests
  integration/test_daemon_lifecycle.cpp integration/test_parallel_execution.cpp
  integration/test_end_to_end.cpp integration/test_cli_commands.cpp
  integration/test_instrument_registry.cpp)

target_link_libraries(
  integration_tests PRIVATE instrument-server-core test-utils GTest::gtest
                            GTest::gtest_main)

gtest_discover_tests(integration_tests)

# Performance tests
add_executable(perf_tests performance/test_ipc_throughput.cpp
                          performance/test_sync_overhead.cpp)

target_link_libraries(perf_tests PRIVATE instrument-server-core test-utils
                                         GTest::gtest GTest::gtest_main)

# Don't auto-run performance tests Run manually with: ./perf_tests

# Mock plugin for testing
add_library(mock_plugin MODULE mocks/mock_plugin.cpp)

target_link_libraries(mock_plugin PRIVATE instrument-server-core)

set_target_properties(mock_plugin PROPERTIES PREFIX "" OUTPUT_NAME
                                                       "mock_plugin")

# Copy test data
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tests/data)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/data/mock_instrument.yaml
               ${CMAKE_BINARY_DIR}/tests/data/mock_instrument.yaml COPYONLY)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/data/mock_api.yaml
               ${CMAKE_BINARY_DIR}/tests/data/mock_api.yaml COPYONLY)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/data/test_script.lua
               ${CMAKE_BINARY_DIR}/tests/data/test_script.lua COPYONLY)

# Install test plugin
install(TARGETS mock_plugin LIBRARY DESTINATION lib/instrument-plugins)

# Add custom target to run only unit tests
add_custom_target(
  test_unit
  COMMAND ${CMAKE_BINARY_DIR}/tests/unit_tests
  DEPENDS unit_tests
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Add custom target to run only integration tests
add_custom_target(
  test_integration
  COMMAND ${CMAKE_BINARY_DIR}/tests/integration_tests
  DEPENDS integration_tests
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

# Add custom target to run performance tests
add_custom_target(
  test_perf
  COMMAND ${CMAKE_BINARY_DIR}/tests/perf_tests
  DEPENDS perf_tests
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
