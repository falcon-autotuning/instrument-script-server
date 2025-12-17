# CMake generated Testfile for 
# Source directory: /home/tylerk/falcon-dev/instrument-script-server/tests
# Build directory: /home/tylerk/falcon-dev/instrument-script-server/build/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[IPCtests]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_ipc")
set_tests_properties([=[IPCtests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;8;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
add_test([=[WorkerIntegrationTests]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_plugin")
set_tests_properties([=[WorkerIntegrationTests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;13;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
add_test([=[ValidatorTests]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_validator")
set_tests_properties([=[ValidatorTests]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;18;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
