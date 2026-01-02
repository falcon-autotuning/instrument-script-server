BUILD_DIR=./build
build:
	mkdir -p build && cd build && CC="ccache clang" CXX="ccache clang++" \
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
