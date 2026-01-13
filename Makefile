# Simple Makefile for local builds (Linux + cross-clang-to-windows)
# Use: 
#   make build            # cross-build for Windows using clang/clang++
#   make clean            # clean build directories
# Optional overrides: 
#   MINGW_SYSROOT=/path/to/sysroot   (defaults to /usr/x86_64-w64-mingw32)
#   BUILD_DIR=./build-win             (defaults to ./build-win for Windows builds)

.PHONY: all build clean unit-test integration-tests perf-tests coverage \
        build-native build-windows wine-unit-test wine-integration-test

BUILD_DIR ?= ./build-win
CMAKE ?= cmake
NINJA ?= ninja
CROSS_GCC ?= x86_64-w64-mingw32-gcc

all: build

# Cross-build for Windows using system x86_64-w64-mingw32 sysroot + clang
MINGW_SYSROOT ?= /usr/x86_64-w64-mingw32
TOOLCHAIN_TEMPLATE ?= cmake/toolchains/mingw-clang-toolchain.cmake.tpl
TMP_TOOLCHAIN=/tmp/mingw-clang-toolchain.cmake

build-windows:
	@echo "Cross-building for Windows using clang -> mingw sysroot:  $(MINGW_SYSROOT)"
	@mkdir -p $(BUILD_DIR)
	@MINGW_SYSROOT='$(MINGW_SYSROOT)' \
	CROSS_GCC='$(CROSS_GCC)' \
	TOOLCHAIN_TEMPLATE='$(TOOLCHAIN_TEMPLATE)' \
	TMP_TOOLCHAIN='$(TMP_TOOLCHAIN)' \
	BUILD_DIR='$(BUILD_DIR)' \
	CMAKE='$(CMAKE)' \
	NINJA='$(NINJA)' \
	./scripts/build-windows.sh

# Native (Linux) build - using clang toolchain and profile flags
build: 
	mkdir -p ./build
	cd ./build && \
	CC="clang" CXX="clang++" \
	$(CMAKE) -G Ninja \
	  -DCMAKE_C_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
	  -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping -Oz -g" \
	  -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld -fprofile-instr-generate" \
	  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .. 
	$(NINJA) -C ./build

clean:
	rm -rf $(BUILD_DIR) ./build $(TMP_TOOLCHAIN)

unit-test:
	LLVM_PROFILE_FILE=./build/unit_tests.profraw PATH=./build:$$PATH ./build/tests/unit_tests

integration-tests:
	LLVM_PROFILE_FILE=./build/integration_tests.profraw PATH=./build:$$PATH ./build/tests/integration_tests

perf-tests:
	LLVM_PROFILE_FILE=./build/perf_tests. profraw PATH=./build:$$PATH ./build/tests/perf_tests

coverage:  build-native unit-test integration-tests
	llvm-profdata merge -sparse ./build/*. profraw -o ./build/instrument_server_core.profdata
	llvm-cov show ./build/libinstrument-server-core. so \
		-instr-profile=./build/instrument_server_core.profdata \
		-ignore-filename-regex='(tests/)' \
		-Xdemangler c++filt -Xdemangler -n

coverage-overview: 
	@llvm-cov report ./build/libinstrument-server-core.so \
		-instr-profile=./build/instrument_server_core. profdata \
		-ignore-filename-regex='(tests/)' \
		-Xdemangler c++filt -Xdemangler -n

# Run tests via wine against Windows cross-built binaries
wine-unit-test:  build-windows
	WINEPATH="$(BUILD_DIR)" wine "$(BUILD_DIR)/tests/unit_tests.exe"

wine-integration-test: build-windows
	WINEPATH="$(BUILD_DIR)" wine "$(BUILD_DIR)/tests/integration_tests.exe"
