find_package(GTest REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(test_ipc test_ipc.cpp)
target_link_libraries(test_ipc PRIVATE instrument_server_lib GTest::gtest
                                       GTest::gtest_main)
add_test(
  NAME IPCtests
  COMMAND test_ipc
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_plugin test_plugin.cpp)
target_link_libraries(test_plugin PRIVATE instrument_server_lib GTest::gtest
                                          GTest::gtest_main)
add_test(
  NAME Plugin
  COMMAND test_plugin
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_executable(test_plugin_loader test_plugin_loader.cpp)
target_link_libraries(test_plugin_loader PRIVATE instrument_server_lib
                                                 GTest::gtest GTest::gtest_main)
add_test(
  NAME PluginLoader
  COMMAND test_plugin_loader
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_executable(test_plugin_interface test_plugin_interface.cpp)
target_link_libraries(
  test_plugin_interface PRIVATE instrument_server_lib GTest::gtest
                                GTest::gtest_main)
add_test(
  NAME PluginInterface
  COMMAND test_plugin_interface
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_instrument_registry test_instrument_registry.cpp)
target_link_libraries(
  test_instrument_registry PRIVATE instrument_server_lib GTest::gtest
                                   GTest::gtest_main)
add_test(
  NAME InstrumentRegistry
  COMMAND test_instrument_registry
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_ipc_full test_ipc_full.cpp)
target_link_libraries(test_ipc_full PRIVATE instrument_server_lib GTest::gtest
                                            GTest::gtest_main)
add_test(
  NAME IPCFull
  COMMAND test_ipc_full
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
# Replace test_ipc_stress with test_ipc_benchmark
add_executable(test_ipc_benchmark test_ipc_benchmark.cpp)
target_link_libraries(test_ipc_benchmark PRIVATE instrument_server_lib
                                                 GTest::GTest GTest::Main)
add_test(NAME IPCBenchmark COMMAND test_ipc_benchmark)

# Set longer timeout for benchmark
set_tests_properties(IPCBenchmark PROPERTIES TIMEOUT 30)

add_executable(test_serialized_command test_serialized_command.cpp)
target_link_libraries(
  test_serialized_command PRIVATE instrument_server_lib GTest::gtest
                                  GTest::gtest_main)
add_test(
  NAME SerializedCommand
  COMMAND test_serialized_command
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_executable(test_worker_protocol test_worker_protocol.cpp)
target_link_libraries(
  test_worker_protocol PRIVATE instrument_server_lib GTest::gtest
                               GTest::gtest_main)
add_test(
  NAME WorkerProtocol
  COMMAND test_worker_protocol
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test_validator test_validator.cpp)
target_link_libraries(test_validator PRIVATE instrument_server_lib GTest::gtest
                                             GTest::gtest_main)
add_test(
  NAME ValidatorTests
  COMMAND test_validator
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(
  ValidatorTests PROPERTIES ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}:$ENV{PATH}")

add_executable(test_mock_integration test_mock_integration.cpp)
target_link_libraries(
  test_mock_integration PRIVATE instrument_server_lib GTest::gtest
                                GTest::gtest_main)
add_test(
  NAME MockIntegration
  COMMAND test_mock_integration
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
# Build mock plugin as part of tests
add_library(mock_visa_plugin SHARED test_mock_plugin.c)
target_include_directories(mock_visa_plugin PRIVATE ${CMAKE_SOURCE_DIR}/include)
set_target_properties(mock_visa_plugin PROPERTIES PREFIX "" OUTPUT_NAME
                                                            "mock_visa_plugin")

# Make mock integration test depend on mock plugin
add_dependencies(test_mock_integration mock_visa_plugin)

# Copy mock plugin to test directory
add_custom_command(
  TARGET mock_visa_plugin
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mock_visa_plugin>
          /tmp/mock_visa_plugin.so)
