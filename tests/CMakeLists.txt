find_package(GTest REQUIRED)

enable_testing()
include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(test_ipc test_ipc.cpp)
target_link_libraries(test_ipc PRIVATE instrument_server_lib GTest::GTest
                                       GTest::Main)
add_test(NAME IPCTests COMMAND test_ipc)

add_executable(test_plugin test_plugin.cpp)
target_link_libraries(test_plugin PRIVATE instrument_server_lib GTest::GTest
                                          GTest::Main)
add_test(NAME WorkerIntegrationTests COMMAND test_worker_integration)

add_executable(test_validator test_validator.cpp)
target_link_libraries(test_validator PRIVATE instrument_server_lib GTest::gtest
                                             GTest::gtest_main)
add_test(NAME ValidatorTests COMMAND test_validator)
