build:
	mkdir -p build && cd build && CC=clang CXX=clang++ cmake -G Ninja -DCMAKE_CXX_FLAGS="-Oz" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .. && ninja
test:
	PATH=./build:$$PATH ./build/test_validator 
clean:
	rm -rf build
