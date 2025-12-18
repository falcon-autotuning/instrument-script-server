# CMake generated Testfile for 
# Source directory: /home/tylerk/falcon-dev/instrument-script-server/tests
# Build directory: /home/tylerk/falcon-dev/instrument-script-server/build/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[IPCtests]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_ipc")
set_tests_properties([=[IPCtests]=] PROPERTIES  WORKING_DIRECTORY "/home/tylerk/falcon-dev/instrument-script-server" _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;8;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
add_test([=[WorkerIntegrationTests]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_plugin")
set_tests_properties([=[WorkerIntegrationTests]=] PROPERTIES  WORKING_DIRECTORY "/home/tylerk/falcon-dev/instrument-script-server" _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;16;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
add_test([=[ValidatorTests]=] "/home/tylerk/falcon-dev/instrument-script-server/build/tests/test_validator")
set_tests_properties([=[ValidatorTests]=] PROPERTIES  ENVIRONMENT "PATH=/home/tylerk/falcon-dev/instrument-script-server/build:/home/tylerk/.local/share/nvim/mason/bin:/usr/local/bin:/usr/bin:/home/tylerk/.cargo/bin/:/home/tylerk/.local/bin/:/home/tylerk/go/bin:/home/tylerk/.cargo/bin/:/home/tylerk/.local/bin/:/home/tylerk/go/bin" WORKING_DIRECTORY "/home/tylerk/falcon-dev/instrument-script-server" _BACKTRACE_TRIPLES "/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;24;add_test;/home/tylerk/falcon-dev/instrument-script-server/tests/CMakeLists.txt;0;")
