build:
	mkdir -p build && cd build && CC="ccache clang" CXX="ccache clang++" cmake -G Ninja -DCMAKE_CXX_FLAGS="-Oz" -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .. && ninja
test:
	PATH=./build:$$PATH ./build/test_validator 
clean:
	rm -rf build
