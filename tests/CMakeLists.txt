find_package(GTest CONFIG REQUIRED)

add_library(test-utils STATIC
  test_utils/TestFixtures.cpp
)

target_link_libraries(test-utils PUBLIC
  instrument-script-server-core
  GTest::gtest
)

target_include_directories(test-utils PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/test_utils
)

add_custom_target(copy_test_data ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/data
    ${CMAKE_BINARY_DIR}/tests/data
)

add_executable(
  unit_tests
  unit/test_serialization.cpp
  unit/test_sync_coordinator.cpp
  unit/test_runtime_context_generic.cpp
  unit/test_ipc_queue.cpp
  unit/test_schema_validator.cpp
  unit/test_plugin_loader.cpp
  unit/test_api_lookup.cpp
  unit/test_data_buffer_manager.cpp
  unit/test_plugin_loading.cpp
  unit/test_api_ref_resolution.cpp
  unit/test_plugin_registry.cpp
  unit/test_main_function.cpp)

target_link_libraries(unit_tests
  PRIVATE
    instrument-script-server-core
    test-utils
    GTest::gtest
    GTest::gtest_main
    instrument-plugin-api::instrument-plugin-api-host
    instrument-plugin-api::instrument-plugin-api-plugin
)

add_executable(
  integration_tests
  integration/test_daemon_lifecycle.cpp
  integration/test_parallel_execution.cpp
  integration/test_cli_commands.cpp
  integration/test_instrument_registry.cpp
  integration/test_measurement_scripts.cpp
  integration/test_visa_large_data.cpp
  integration/test_rpc_server.cpp
  integration/test_measure_command.cpp
  integration/test_main_function_integration.cpp
  integration/test_type_manifest.cpp)

target_link_libraries(integration_tests
  PRIVATE
    instrument-script-server-core
    test-utils
    GTest::gtest
    GTest::gtest_main
    instrument-plugin-api::instrument-plugin-api-host
)

add_executable(
  perf_tests
  performance/test_ipc_throughput.cpp
  performance/test_sync_overhead.cpp
  performance/test_end_to_end_overhead.cpp)

target_link_libraries(perf_tests PRIVATE
  instrument-script-server-core
  test-utils
  GTest::gtest
  GTest::gtest_main
)

foreach(tgt unit_tests integration_tests perf_tests)
  target_include_directories(${tgt} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/test_utils
  )
endforeach()


foreach(tgt unit_tests integration_tests perf_tests)
  target_compile_definitions(${tgt} PRIVATE
    ISS_BIN_PATH="$<TARGET_FILE:instrument-script-server>"
  )
  add_test(NAME ${tgt} COMMAND ${tgt})

  set_tests_properties(${tgt} PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/tests
  )

  if(WIN32)
    set_tests_properties(${tgt} PROPERTIES
      ENVIRONMENT "PATH=$<TARGET_FILE_DIR:${tgt}>;$<TARGET_FILE_DIR:instrument-script-server-core>;$ENV{PATH}"
    )
  endif()
endforeach()


add_library(mock_plugin SHARED mocks/mock_plugin.cpp)
add_library(mock_visa_plugin SHARED mocks/mock_visa_plugin.c)
add_library(mock_visa_large_data_plugin SHARED
  mocks/mock_visa_large_data_plugin.c)
foreach(plugin_target
    mock_plugin
    mock_visa_plugin
    mock_visa_large_data_plugin
)
  target_compile_definitions(${plugin_target} PRIVATE INSTRUMENT_PLUGIN_BUILD)
  target_link_libraries(${plugin_target}
    PRIVATE
      instrument-script-server-core
      instrument-plugin-api::instrument-plugin-api-plugin
  )
endforeach()
if(ENABLE_ASAN)
  foreach(plugin_target
      mock_plugin
      mock_visa_plugin
      mock_visa_large_data_plugin
  )
    target_compile_options(${plugin_target} PRIVATE -fno-sanitize=all)
    target_link_options(${plugin_target} PRIVATE -fno-sanitize=all)
  endforeach()
endif()


set(TEST_RUNTIME_TOOLS
  instrument-worker
  validate-instrument-api
  validate-instrument-config
  validate-quantum-dot-config
  template-expander
  generate-instrument-config
)

set(TEST_PLUGINS
  mock_plugin
  mock_visa_plugin
  mock_visa_large_data_plugin
)

if(WIN32)
  foreach(tgt unit_tests integration_tests perf_tests)
    add_dependencies(${tgt} ${TEST_RUNTIME_TOOLS})
    add_custom_command(TARGET ${tgt} POST_BUILD

      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:${tgt}>
        $<TARGET_FILE_DIR:${tgt}>
      COMMAND_EXPAND_LISTS
    )

    foreach(tool ${TEST_RUNTIME_TOOLS})
      add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:${tool}>"
          "$<TARGET_FILE_DIR:${tgt}>"
        DEPENDS ${tool}
      )
    endforeach()

    foreach(plugin ${TEST_PLUGINS})
      add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:${plugin}>"
          "$<TARGET_FILE_DIR:${tgt}>"
      )
    endforeach()

  endforeach()
endif()
add_dependencies(unit_tests copy_test_data)
add_dependencies(integration_tests copy_test_data)
add_dependencies(perf_tests copy_test_data)
