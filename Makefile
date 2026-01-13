# Simple Makefile for local builds (Linux + cross-clang-to-windows)
# Use:
#   make build            # native/linux build (clang)
#   make build-win-clang  # cross-build for Windows using clang/clang++
# Optional overrides:
#   MINGW_SYSROOT=/path/to/sysroot   (defaults to /usr/x86_64-w64-mingw32)
#   BUILD_DIR=./build                 (defaults to ./build)
#
# Example:
#   make build-win-clang MINGW_SYSROOT=/home/me/mingw-sysroot

.PHONY: all build clean unit-test integration-tests perf-tests coverage \
        build-win build-win-clang wine-unit-test wine-integration-test

BUILD_DIR ?= ./build
CMAKE ?= cmake
NINJA ?= ninja

all: build

# Native (Linux) build - using clang toolchain and profile flags as before
build:
	mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && \
	CC="clang" CXX="clang++" \
	$(CMAKE) -G Ninja \
	  -DCMAKE_C_FLAGS="-fprofile-instr-generate -fcoverage-mapping" \
	  -DCMAKE_CXX_FLAGS="-fprofile-instr-generate -fcoverage-mapping -Oz -g" \
	  -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld -fprofile-instr-generate" \
	  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .. && $(NINJA) -C $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR) build-win

unit-test:
	LLVM_PROFILE_FILE=$(BUILD_DIR)/unit_tests.profraw PATH=./$(BUILD_DIR):$$PATH ./$(BUILD_DIR)/tests/unit_tests

integration-tests:
	LLVM_PROFILE_FILE=$(BUILD_DIR)/integration_tests.profraw PATH=./$(BUILD_DIR):$$PATH ./$(BUILD_DIR)/tests/integration_tests

perf-tests:
	LLVM_PROFILE_FILE=$(BUILD_DIR)/perf_tests.profraw PATH=./$(BUILD_DIR):$$PATH ./$(BUILD_DIR)/tests/perf_tests

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

# Cross-build for Windows using system x86_64-w64-mingw32 sysroot + clang
# This target writes a temporary toolchain file to /tmp and invokes CMake with it.
MINGW_SYSROOT ?= /usr/x86_64-w64-mingw32
BUILD_DIR_WIN ?= build-win

build-win-clang:
	@echo "Cross-building for Windows using clang -> mingw sysroot: $(MINGW_SYSROOT)"
	@mkdir -p $(BUILD_DIR_WIN)
	@bash -lc "\
MINGW_SYSROOT='$(MINGW_SYSROOT)'; \
MINGW_GCC_LIBDIR=''; \
CANDIDATES=( \
  '/usr/lib/gcc/x86_64-w64-mingw32' \
  '/usr/lib/gcc/x86_64-w64-mingw32/'* \
  \"\${MINGW_SYSROOT}/lib/gcc/x86_64-w64-mingw32\" \
  \"\${MINGW_SYSROOT}/lib/gcc/x86_64-w64-mingw32/\"* \
); \
for d in \"\${CANDIDATES[@]}\"; do \
  if [ -d \"\$d\" ] && ls \"\$d\"/libgcc* >/dev/null 2>&1; then \
    MINGW_GCC_LIBDIR=\"\$d\"; break; \
  fi; \
done; \
if [ -n \"\$MINGW_GCC_LIBDIR\" ]; then echo \"Detected MINGW_GCC_LIBDIR=\$MINGW_GCC_LIBDIR\"; fi; \
cat > /tmp/mingw-clang-toolchain.cmake <<'EOF'
# clang -> mingw-w64 sysroot toolchain for x86_64-w64-mingw32
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_SYSROOT $(MINGW_SYSROOT))
set(CMAKE_FIND_ROOT_PATH \${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CLANG_TARGET \"--target=x86_64-w64-mingw32\")
set(CLANG_SYSROOT \"--sysroot=\${CMAKE_SYSROOT}\")
EOF; \
if [ -n \"\$MINGW_GCC_LIBDIR\" ]; then \
  cat >> /tmp/mingw-clang-toolchain.cmake <<EOF
set(CMAKE_C_FLAGS \"\${CLANG_TARGET} \${CLANG_SYSROOT} --gcc-toolchain=\${CMAKE_SYSROOT} \${CMAKE_C_FLAGS}\")
set(CMAKE_CXX_FLAGS \"\${CLANG_TARGET} \${CLANG_SYSROOT} --gcc-toolchain=\${CMAKE_SYSROOT} \${CMAKE_CXX_FLAGS}\")
set(CMAKE_EXE_LINKER_FLAGS \"\${CLANG_TARGET} \${CLANG_SYSROOT} -fuse-ld=lld -L${MINGW_GCC_LIBDIR} \${CMAKE_EXE_LINKER_FLAGS}\")
set(CMAKE_SHARED_LINKER_FLAGS \"\${CMAKE_EXE_LINKER_FLAGS}\")
EOF; \
else \
  cat >> /tmp/mingw-clang-toolchain.cmake <<EOF
set(CMAKE_C_FLAGS \"\${CLANG_TARGET} \${CLANG_SYSROOT} \${CMAKE_C_FLAGS}\")
set(CMAKE_CXX_FLAGS \"\${CLANG_TARGET} \${CLANG_SYSROOT} \${CMAKE_CXX_FLAGS}\")
set(CMAKE_EXE_LINKER_FLAGS \"\${CLANG_TARGET} \${CLANG_SYSROOT} -fuse-ld=lld \${CMAKE_EXE_LINKER_FLAGS}\")
set(CMAKE_SHARED_LINKER_FLAGS \"\${CMAKE_EXE_LINKER_FLAGS}\")
EOF; \
fi; \
echo 'Using toolchain file: /tmp/mingw-clang-toolchain.cmake'; \
$(CMAKE) -S . -B $(BUILD_DIR_WIN) -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=/tmp/mingw-clang-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && $(NINJA) -C $(BUILD_DIR_WIN)"

# Old build-win target kept for GCC-based mingw cross (preserved for convenience)
build-win:
	mkdir -p $(BUILD_DIR_WIN) && cd $(BUILD_DIR_WIN) && \
	$(CMAKE) -G Ninja \
		-DCMAKE_SYSTEM_NAME=Windows \
		-DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
		-DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
		-DCMAKE_BUILD_TYPE=Release \
		.. && $(NINJA) -C $(BUILD_DIR_WIN)

# Run tests via wine against Windows cross-built binaries (clang or gcc builds)
wine-unit-test: build-win
	WINEPATH="$(BUILD_DIR_WIN)" wine "$(BUILD_DIR_WIN)/tests/unit_tests.exe"

wine-integration-test: build-win
	WINEPATH="$(BUILD_DIR_WIN)" wine "$(BUILD_DIR_WIN)/tests/integration_tests.exe"

wine-unit-test-clang: build-win-clang
	WINEPATH="$(BUILD_DIR_WIN)" wine "$(BUILD_DIR_WIN)/tests/unit_tests.exe"

wine-integration-test-clang: build-win-clang
	WINEPATH="$(BUILD_DIR_WIN)" wine "$(BUILD_DIR_WIN)/tests/integration_tests.exe"
