# CMake generated Testfile for 
# Source directory: /home/tylerk/falcon-dev/instrument-script-server/tests
# Build directory: /home/tylerk/falcon-dev/instrument-script-server/build/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[IPCtests]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_ipc")
set_tests_properties([=[IPCtests]=] PROPERTIES  WORKING_DIRECTORY "/home/tylerk/falcon-dev/instrument-script-server" _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;8;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
add_test([=[Plugin]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_plugin")
set_tests_properties([=[Plugin]=] PROPERTIES  WORKING_DIRECTORY "/home/tylerk/falcon-dev/instrument-script-server" _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;16;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
add_test([=[PluginLoader]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_plugin_loader")
set_tests_properties([=[PluginLoader]=] PROPERTIES  WORKING_DIRECTORY "/home/tylerk/falcon-dev/instrument-script-server" _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;23;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
add_test([=[PluginInterface]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_plugin_interface")
set_tests_properties([=[PluginInterface]=] PROPERTIES  WORKING_DIRECTORY "/home/tylerk/falcon-dev/instrument-script-server" _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;31;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
add_test([=[InstrumentRegistry]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_instrument_registry")
set_tests_properties([=[InstrumentRegistry]=] PROPERTIES  WORKING_DIRECTORY "/home/tylerk/falcon-dev/instrument-script-server" _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;40;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
add_test([=[IPCFull]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_ipc_full")
set_tests_properties([=[IPCFull]=] PROPERTIES  WORKING_DIRECTORY "/home/tylerk/falcon-dev/instrument-script-server" _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;48;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
add_test([=[IPCBenchmark]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_ipc_benchmark")
set_tests_properties([=[IPCBenchmark]=] PROPERTIES  TIMEOUT "30" _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;56;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
add_test([=[SerializedCommand]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_serialized_command")
set_tests_properties([=[SerializedCommand]=] PROPERTIES  WORKING_DIRECTORY "/home/tylerk/falcon-dev/instrument-script-server" _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;65;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
add_test([=[WorkerProtocol]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_worker_protocol")
set_tests_properties([=[WorkerProtocol]=] PROPERTIES  WORKING_DIRECTORY "/home/tylerk/falcon-dev/instrument-script-server" _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;73;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
add_test([=[ValidatorTests]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_validator")
set_tests_properties([=[ValidatorTests]=] PROPERTIES  ENVIRONMENT "PATH=/home/tylerk/falcon-dev/instrument-script-server/build:/home/tylerk/.local/share/nvim/mason/bin:/usr/local/bin:/usr/bin:/home/tylerk/.cargo/bin/:/home/tylerk/.local/bin/:/home/tylerk/go/bin:/home/tylerk/.cargo/bin/:/home/tylerk/.local/bin/:/home/tylerk/go/bin" WORKING_DIRECTORY "/home/tylerk/falcon-dev/instrument-script-server" _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;81;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
add_test([=[MockIntegration]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_mock_integration")
set_tests_properties([=[MockIntegration]=] PROPERTIES  WORKING_DIRECTORY "/home/tylerk/falcon-dev/instrument-script-server" _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;92;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
