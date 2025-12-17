find_package(GTest REQUIRED)

enable_testing()

add_executable(test_ipc test_ipc.cpp)
target_link_libraries(test_ipc PRIVATE instrument_script GTest::GTest
                                       GTest::Main)
add_test(NAME IPCTests COMMAND test_ipc)

add_executable(test_plugin test_plugin.cpp)
target_link_libraries(test_plugin PRIVATE instrument_script GTest::GTest
                                          GTest::Main)
add_test(NAME PluginTests COMMAND test_plugin)

add_executable(test_worker_integration test_worker_integration.cpp)
target_link_libraries(test_worker_integration PRIVATE instrument_script
                                                      GTest::GTest GTest::Main)
add_test(NAME WorkerIntegrationTests COMMAND test_worker_integration)
