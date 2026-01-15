find_package(GTest REQUIRED)
include(GoogleTest)

add_library(test-utils STATIC test_utils/MockInstrument.cpp
                              test_utils/TestFixtures.cpp)
target_link_libraries(test-utils PUBLIC instrument-server-core GTest::gtest)
target_include_directories(test-utils
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/test_utils)

add_executable(
  unit_tests
  unit/test_serialization.cpp
  unit/test_sync_coordinator.cpp
  unit/test_runtime_context_generic.cpp
  unit/test_ipc_queue.cpp
  unit/test_server_daemon.cpp
  unit/test_schema_validator.cpp
  unit/test_plugin_loader.cpp
  unit/test_api_lookup.cpp
  unit/test_data_buffer_manager.cpp
  unit/test_plugin_loading.cpp
  unit/test_api_ref_resolution.cpp
  unit/test_plugin_registry.cpp)
target_link_libraries(unit_tests PRIVATE instrument-server-core test-utils
                                         GTest::gtest GTest::gtest_main)
if(WIN32)
  add_test(NAME unit_tests COMMAND unit_tests)
else()
  gtest_discover_tests(unit_tests)
endif()

add_executable(
  integration_tests
  integration/test_daemon_lifecycle.cpp
  integration/test_parallel_execution.cpp
  integration/test_end_to_end.cpp
  integration/test_cli_commands.cpp
  integration/test_instrument_registry.cpp
  integration/test_measurement_scripts.cpp
  integration/test_visa_large_data.cpp
  integration/test_rpc_server.cpp
  integration/test_measure_command.cpp)

target_link_libraries(
  integration_tests PRIVATE instrument-server-core test-utils GTest::gtest
                            GTest::gtest_main)

# Link math library only on Unix (Linux/macOS) - Windows has math in CRT
if(UNIX)
  target_link_libraries(integration_tests PRIVATE m)
endif()

if(WIN32)
  add_test(NAME integration_tests COMMAND integration_tests)
else()
  gtest_discover_tests(integration_tests)
endif()

add_executable(
  perf_tests
  performance/test_ipc_throughput.cpp performance/test_sync_overhead.cpp
  performance/test_end_to_end_overhead.cpp)
target_link_libraries(perf_tests PRIVATE instrument-server-core test-utils
                                         GTest::gtest GTest::gtest_main)

if(WIN32)
  add_test(NAME perf_tests COMMAND perf_tests)
else()
  gtest_discover_tests(perf_tests)
endif()

add_library(mock_plugin SHARED mocks/mock_plugin.cpp)
target_compile_definitions(mock_plugin PRIVATE INSTRUMENT_PLUGIN_EXPORTS)
target_link_libraries(mock_plugin PRIVATE instrument-server-core)
set_target_properties(mock_plugin PROPERTIES PREFIX "" POSITION_INDEPENDENT_CODE
                                                       ON)

add_library(mock_visa_plugin SHARED mocks/mock_visa_plugin.c)
target_compile_definitions(mock_visa_plugin PRIVATE INSTRUMENT_PLUGIN_EXPORTS)
target_link_libraries(mock_visa_plugin PRIVATE instrument-server-core)
set_target_properties(mock_visa_plugin PROPERTIES PREFIX ""
                                                  POSITION_INDEPENDENT_CODE ON)

add_library(mock_visa_large_data_plugin SHARED
            mocks/mock_visa_large_data_plugin.c)
target_compile_definitions(mock_visa_large_data_plugin
                           PRIVATE INSTRUMENT_PLUGIN_EXPORTS)
target_link_libraries(mock_visa_large_data_plugin
                      PRIVATE instrument-server-core)
set_target_properties(mock_visa_large_data_plugin
                      PROPERTIES PREFIX "" POSITION_INDEPENDENT_CODE ON)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tests/data)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/tests/data/test_scripts)
set(TEST_SCRIPTS
    simple_call.lua
    parallel_test.lua
    loop_measurement.lua
    nested_parallel.lua
    error_handling.lua
    channel_addressing.lua
    return_types.lua
    table_params.lua
    multiple_returns.lua
    large_buffer_returns.lua)
foreach(SCRIPT ${TEST_SCRIPTS})
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/data/test_scripts/${SCRIPT}
                 ${CMAKE_BINARY_DIR}/tests/data/test_scripts/${SCRIPT} COPYONLY)
endforeach()
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/data/mock_instrument1.yaml
               ${CMAKE_BINARY_DIR}/tests/data/mock_instrument1.yaml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/data/mock_instrument2.yaml
               ${CMAKE_BINARY_DIR}/tests/data/mock_instrument2.yaml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/data/mock_instrument3.yaml
               ${CMAKE_BINARY_DIR}/tests/data/mock_instrument3.yaml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/data/mock_api.yaml
               ${CMAKE_BINARY_DIR}/tests/data/mock_api.yaml COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/data/test_script.lua
               ${CMAKE_BINARY_DIR}/tests/data/test_script.lua COPYONLY)

install(TARGETS mock_plugin LIBRARY DESTINATION lib/instrument-plugins)

add_custom_target(
  test_unit
  COMMAND ${CMAKE_BINARY_DIR}/tests/unit_tests
  DEPENDS unit_tests
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_custom_target(
  test_integration
  COMMAND ${CMAKE_BINARY_DIR}/tests/integration_tests
  DEPENDS integration_tests
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
add_custom_target(
  test_perf
  COMMAND ${CMAKE_BINARY_DIR}/tests/perf_tests
  DEPENDS perf_tests
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
include(GoogleTest)
