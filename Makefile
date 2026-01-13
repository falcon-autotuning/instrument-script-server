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
CROSS_GCC ?= $(shell command -v x86_64-w64-mingw32-gcc 2>/dev/null || true)

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
TOOLCHAIN_TEMPLATE ?= cmake/toolchains/mingw-clang-toolchain.cmake.tpl
TMP_TOOLCHAIN=/tmp/mingw-clang-toolchain.cmake

# Replace the build-win-clang recipe with this:
build-win-clang:
	@echo "Cross-building for Windows using clang -> mingw sysroot: $(MINGW_SYSROOT)"
	@mkdir -p $(BUILD_DIR_WIN)
	@bash -lc "\
MINGW_SYSROOT='$(MINGW_SYSROOT)'; \
CROSS_GCC='$(CROSS_GCC)'; \
MINGW_GCC_LIBDIR=''; \
echo 'CROSS_GCC=(make-level) $$CROSS_GCC'; \
if [ -n \"$$CROSS_GCC\" ] && [ -x \"$$CROSS_GCC\" ]; then \
  echo "Using CROSS_GCC: $$CROSS_GCC"; \
  libgcc_path=\"$$( $$CROSS_GCC -print-file-name=libgcc.a 2>/dev/null || true )\"; \
  if [ -f \"$$libgcc_path\" ]; then \
    MINGW_GCC_LIBDIR=\"$$(dirname \"$$libgcc_path\")\"; \
    echo \"  libgcc located via cross-gcc at: $$libgcc_path\"; \
  else \
    echo \"  cross-gcc present but did not return libgcc path (got: '$$libgcc_path')\"; \
  fi; \
else \
  echo 'Cross-gcc not provided or not executable; will probe fallback locations'; \
fi; \
if [ -z \"$$MINGW_GCC_LIBDIR\" ]; then \
  CANDIDATES=( \
    '/usr/lib/gcc/x86_64-w64-mingw32' \
    '/usr/lib/gcc' \
    '$(MINGW_SYSROOT)/lib/gcc/x86_64-w64-mingw32' \
    '$(MINGW_SYSROOT)/lib/gcc' \
    '$(MINGW_SYSROOT)/usr/lib/gcc/x86_64-w64-mingw32' \
    '$(MINGW_SYSROOT)/usr/lib/gcc' \
    '/usr/x86_64-w64-mingw32/lib' \
    '/usr/x86_64-w64-mingw32/usr/lib' \
  ); \
  for d in \"$${CANDIDATES[@]}\"; do \
    echo \"  checking: $$d\"; \
    if [ -d \"$$d\" ]; then \
      if ls \"$$d\"/libgcc* >/dev/null 2>&1 || ls \"$$d\"/*/libgcc* >/dev/null 2>&1; then \
        if ls \"$$d\"/*/libgcc* >/dev/null 2>&1; then \
          for sub in \"$$d\"/*; do \
            if ls \"$$sub\"/libgcc* >/dev/null 2>&1; then MINGW_GCC_LIBDIR=\"$$sub\"; break; fi; \
          done; \
        else \
          MINGW_GCC_LIBDIR=\"$$d\"; \
        fi; \
        break; \
      fi; \
    fi; \
  done; \
fi; \
if [ -n \"$$MINGW_GCC_LIBDIR\" ]; then \
  echo \"Detected MINGW_GCC_LIBDIR=$$MINGW_GCC_LIBDIR\"; \
  MINGW_GCC_LDFLAGS='-L'\"$$MINGW_GCC_LIBDIR\"; \
  TOOLCHAIN_ROOT=\"$$(printf '%s' \"$$MINGW_GCC_LIBDIR\" | sed -E 's#/lib/gcc.*##')\"; \
  if [ -d \"$$TOOLCHAIN_ROOT\" ]; then \
    echo \"Using TOOLCHAIN_ROOT=$$TOOLCHAIN_ROOT (for --gcc-toolchain)\"; \
    MINGW_GCC_TOOLCHAIN_FLAG='--gcc-toolchain='\"$$TOOLCHAIN_ROOT\"; \
  else \
    echo 'Could not determine TOOLCHAIN_ROOT; leaving --gcc-toolchain empty'; \
    MINGW_GCC_TOOLCHAIN_FLAG=''; \
  fi; \
else \
  echo 'No MINGW_GCC_LIBDIR detected; continuing without --gcc-toolchain/-L'; \
  MINGW_GCC_LDFLAGS=''; MINGW_GCC_TOOLCHAIN_FLAG=''; \
fi; \
# Use Make-expanded MINGW_SYSROOT directly (guaranteed set in Makefile) for substitution \
sed -e \"s|@MINGW_SYSROOT@|$(MINGW_SYSROOT)|g\" \
    -e \"s|@MINGW_GCC_LDFLAGS@|$$MINGW_GCC_LDFLAGS|g\" \
    -e \"s|@MINGW_GCC_TOOLCHAIN_FLAG@|$$MINGW_GCC_TOOLCHAIN_FLAG|g\" \
    \"$(TOOLCHAIN_TEMPLATE)\" > \"$(TMP_TOOLCHAIN)\"; \
echo 'Wrote toolchain to $(TMP_TOOLCHAIN)'; sed -n '1,120p' \"$(TMP_TOOLCHAIN)\"; \
$(CMAKE) -S . -B $(BUILD_DIR_WIN) -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=\"$(TMP_TOOLCHAIN)\" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && $(NINJA) -C $(BUILD_DIR_WIN)"

# Run tests via wine against Windows cross-built binaries (clang or gcc builds)
wine-unit-test: build-win
	WINEPATH="$(BUILD_DIR_WIN)" wine "$(BUILD_DIR_WIN)/tests/unit_tests.exe"

wine-integration-test: build-win
	WINEPATH="$(BUILD_DIR_WIN)" wine "$(BUILD_DIR_WIN)/tests/integration_tests.exe"

wine-unit-test-clang: build-win-clang
	WINEPATH="$(BUILD_DIR_WIN)" wine "$(BUILD_DIR_WIN)/tests/unit_tests.exe"

wine-integration-test-clang: build-win-clang
	WINEPATH="$(BUILD_DIR_WIN)" wine "$(BUILD_DIR_WIN)/tests/integration_tests.exe"
