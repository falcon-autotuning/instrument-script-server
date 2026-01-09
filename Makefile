BUILD_DIR=./build
build:
	mkdir -p build && cd build && CC="clang" CXX="clang++" \
	cmake -G Ninja \
	-DCMAKE_C_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
	-DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping -Oz -g" \
	-DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld -fprofile-instr-generate" \
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON .. && ninja
clean:
	rm -rf build

unit-test:
	LLVM_PROFILE_FILE=$(BUILD_DIR)/unit_tests.profraw PATH=./build:$$PATH ./build/tests/unit_tests

integration-tests:
	LLVM_PROFILE_FILE=$(BUILD_DIR)/integration_tests.profraw PATH=./build:$$PATH ./build/tests/integration_tests

perf-tests:
	LLVM_PROFILE_FILE=$(BUILD_DIR)/perf_tests.profraw PATH=./build:$$PATH ./build/tests/perf_tests

coverage: build unit-test integration-tests
	llvm-profdata merge -sparse $(BUILD_DIR)/*.profraw -o $(BUILD_DIR)/instrument_server_core.profdata
	llvm-cov show $(BUILD_DIR)/libinstrument-server-core.so \
		-instr-profile=$(BUILD_DIR)/instrument_server_core.profdata \
		-ignore-filename-regex='(tests/)' \
		-Xdemangler c++filt -Xdemangler -n

coverage-overview: 
	@llvm-cov report $(BUILD_DIR)/libinstrument-server-core.so \
		-instr-profile=$(BUILD_DIR)/instrument_server_core.profdata \
		-ignore-filename-regex='(tests/)' \
		-Xdemangler c++filt -Xdemangler -n

build-win:
	mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && \
	cmake -G Ninja \
		-DCMAKE_SYSTEM_NAME=Windows \
		-DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
		-DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
		-DCMAKE_BUILD_TYPE=Release \
		.. && ninja

wine-unit-test: build-win
	WINEPATH="$(BUILD_DIR)" wine $(BUILD_DIR)/tests/unit_tests.exe

wine-integration-test: build-win
	WINEPATH="$(BUILD_DIR)" wine $(BUILD_DIR)/tests/integration_tests.exe

clean:
	rm -rf build build-win
