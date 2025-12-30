build:
	mkdir -p build && cd build && CC="ccache clang" CXX="ccache clang++" cmake -G Ninja -DCMAKE_CXX_FLAGS="-Oz" -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .. && ninja
clean:
	rm -rf build

unit-test:
	PATH=./build:$PATH ./build/tests/unit_tests

integration-tests:
	PATH=./build:$PATH ./build/tests/integration_tests
