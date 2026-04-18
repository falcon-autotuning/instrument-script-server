
.PHONY: all configure build-debug build-release test clean install help check-vcpkg

# Platform detection (works on Linux, MINGW/MSYS, and native Windows)
UNAME_S := $(shell uname -s 2>/dev/null || echo Unknown)
IS_MINGW := $(findstring MINGW,$(UNAME_S))
IS_CYGWIN := $(findstring CYGWIN,$(UNAME_S))
IS_WINDOWS_NT := $(filter Windows_NT,$(OS))
ifeq ($(or $(IS_MINGW),$(IS_CYGWIN),$(IS_WINDOWS_NT)),)
  PLATFORM := linux
else
  PLATFORM := windows
endif

# Default compilers (user can override from environment)
ifeq ($(PLATFORM),windows)
  # prefer clang-cl when available; user can pass CC/ CXX to override
  CMAKE_GENERATOR := Ninja
  VCPKG_TRIPLET := x64-win-llvm
  VCPKG_DEBUG_BIN := $(PWD)/vcpkg_installed/x64-windows/bin
  VCPKG_RELEASE_LIB := $(PWD)/vcpkg_installed/x64-windows/lib
  EXE_SUFFIX := .exe
	NPROC := $(shell powershell -Command "[Environment]::ProcessorCount" 2>NUL || echo 4)
  STRIP_CMD := # no-op (strip not usually present); set to "llvm-strip" if you have it
	RUN_PREFIX := PATH=$(VCPKG_DEBUG_BIN):$(VCPKG_RELEASE_LIB):$$PATH
	SUDO ?= 
  PYTHON_EXECUTABLE ?= python
  # On Windows, Ninja + clang-cl: still pass CMAKE_C_COMPILER / CMAKE_CXX_COMPILER
  export CC=clang-cl
	export CXX=clang-cl
else
  CMAKE_GENERATOR := Ninja
  VCPKG_TRIPLET := x64-linux-dynamic
  VCPKG_DEBUG_LIB := $(PWD)/vcpkg_installed/x64-linux-dynamic/debug/lib
  VCPKG_RELEASE_LIB := $(PWD)/vcpkg_installed/x64-linux-dynamic/lib
  EXE_SUFFIX :=
  NPROC := $(shell nproc 2>/dev/null || echo 4)
  STRIP_CMD := strip
	RUN_PREFIX := LD_LIBRARY_PATH=$(VCPKG_DEBUG_LIB):$(VCPKG_RELEASE_LIB):$$LD_LIBRARY_PATH
	SUDO := sudo
	PYTHON_EXECUTABLE ?= python3
	export CC=clang
	export CXX=clang++
endif

# Paths
ENV_FILE := .nuget-credentials
ifeq ($(wildcard $(ENV_FILE)),)
  $(info [Makefile] $(ENV_FILE) not found, skipping environment sourcing)
else
  include $(ENV_FILE)
  export $(shell sed 's/=.*//' $(ENV_FILE) | xargs)
  $(info [Makefile] Loaded environment from $(ENV_FILE))
endif
# ── Paths ─────────────────────────────────────────────────────────────────────
VCPKG_ROOT ?= $(CURDIR)/vcpkg
VCPKG_TOOLCHAIN ?= $(VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake
VCPKG_INSTALLED_DIR ?= $(CURDIR)/vcpkg_installed
FEED_URL ?= 
NUGET_API_KEY ?=
FEED_NAME ?= 
USERNAME ?=
VCPKG_BINARY_SOURCES ?= 
ifeq ($(strip $(FEED_URL)),)
  CMAKE_VCPKG_BINARY_SOURCES :=
else
	VCPKG_BINARY_SOURCES := "nuget,$(FEED_URL),readwrite"
  CMAKE_VCPKG_BINARY_SOURCES := -DVCPKG_BINARY_SOURCES=$(VCPKG_BINARY_SOURCES)
endif
LINKER_FLAGS ?=
ifeq ($(PLATFORM),linux)
	LINKER_FLAGS := -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld" -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=lld"
endif
VCPKG_OVERLAY_TRIPS ?=
ifeq ($(PLATFORM),windows)
	VCPKG_OVERLAY_TRIPS := -DVCPKG_OVERLAY_TRIPLETS=../../my-vcpkg-triplets
endif

BUILD_DIR_DEBUG := build/debug
BUILD_DIR_RELEASE := build/release

INSTALL_PREFIX    ?= /opt/falcon
INSTALL_LIBDIR    := $(INSTALL_PREFIX)/lib
INSTALL_INCLUDEDIR := $(INSTALL_PREFIX)/include
INSTALL_CMAKEDIR  := $(INSTALL_LIBDIR)/cmake/falcon-routine

# Default target
all: build-release

help:
	@echo "Instrument Script Server Build System"
	@echo "===================================="
	@echo ""
	@echo "Build targets:"
	@echo "  make build-debug        - Build debug version"
	@echo "  make build-release      - Build release version"
	@echo "  make configure          - Configure both debug and release builds"
	@echo "  make install            - Install the library"
	@echo ""
	@echo "Clean targets:"
	@echo "  make clean              - Clean build artifacts and test containers"
	@echo ""
	@echo "Test targets:"
	@echo "  make test               - Run tests" 
	@echo "  make test-debug         - Run debug tests"
	@echo ""
	@echo "Environment variables:"
	@echo "  VCPKG_ROOT              - Path to vcpkg root (default: ../.vcpkg)"
	@echo "  VCPKG_TRIPLET           - vcpkg triplet (default: x64-linux-dynamic)"
	@echo ""
	@echo "Current configuration:"
	@echo "  Platform: $(PLATFORM)"
	@echo "  Generator: $(CMAKE_GENERATOR)"
	@echo "  Triplet: $(VCPKG_TRIPLET)"

.PHONY: vcpkg-bootstrap
vcpkg-bootstrap:
	@if [ ! -d "$(VCPKG_ROOT)" ]; then \
		echo "Cloning vcpkg..."; \
		git clone https://github.com/microsoft/vcpkg.git $(VCPKG_ROOT); \
	fi
	@if [ ! -f "$(VCPKG_ROOT)/vcpkg" ] && [ ! -f "$(VCPKG_ROOT)/vcpkg.exe" ]; then \
		echo "Bootstrapping vcpkg..."; \
		UNAME="$$(uname -s 2>/dev/null || echo Unknown)"; \
		if echo "$$UNAME" | grep -i -q 'mingw\|msys\|cygwin'; then \
			echo "Detected Windows bash environment ($$UNAME). Using cmd.exe to launch bootstrap-vcpkg.bat"; \
			BAT_PATH="$$(cygpath -w "$(VCPKG_ROOT)/bootstrap-vcpkg.bat")"; \
			cmd.exe //C "$$BAT_PATH"; \
			git clone https://github.com/Neumann-A/my-vcpkg-triplets.git || true; \
		else \
			echo "Detected Unix environment ($$UNAME). Using bootstrap-vcpkg.sh"; \
			cd $(VCPKG_ROOT) && ./bootstrap-vcpkg.sh; \
		fi \
	fi

setup-nuget-auth:
	@if [ -z "$$NUGET_API_KEY" ]; then \
		echo "No .nuget_api_key or NUGET_API_KEY found, skipping NuGet setup (local-only build, no binary cache)."; \
		exit 0; \
	fi
	@echo "Setting up NuGet authentication for vcpkg binary caching..."
	@if [ "$$(uname -s 2>/dev/null)" != "Windows_NT" ] && [ "$$(uname -o 2>/dev/null)" != "Msys" ] && [ "$$(uname -o 2>/dev/null)" != "Cygwin" ]; then \
		if ! command -v mono >/dev/null 2>&1; then \
			echo "Error: mono is not installed. Please install mono (e.g., 'sudo pacman -S mono' on Arch, 'sudo apt install mono-complete' on Ubuntu)."; \
			exit 1; \
		fi; \
	fi
	@NUGET_EXE=$$(vcpkg fetch nuget | tail -n1); \
	if [ "$$(uname -s 2>/dev/null)" = "Linux" ]; then \
		MONO_PREFIX="mono "; \
	else \
		MONO_PREFIX=""; \
	fi; \
	$$MONO_PREFIX"$$NUGET_EXE" sources remove -Name "$(FEED_NAME)" || true; \
	$$MONO_PREFIX"$$NUGET_EXE" sources add -Name "$(FEED_NAME)" -Source "$(FEED_URL)" -Username "$(USERNAME)" -Password "$(NUGET_API_KEY)";

.PHONY: vcpkg-install-deps
vcpkg-install-deps: setup-nuget-auth 
	@echo "Installing vcpkg dependencies" 
	VCPKG_FEATURE_FLAGS=binarycaching VCPKG_OVERLAY_TRIPLETS=my-vcpkg-triplets \
		$(VCPKG_ROOT)/vcpkg install \
		--binarysource="$(VCPKG_BINARY_SOURCES)" \
		--triplet="$(VCPKG_TRIPLET)" \
		--vcpkg-root="$(VCPKG_ROOT)"

check-vcpkg: vcpkg-bootstrap  vcpkg-install-deps
	@echo "Checking vcpkg configuration..."
	@if [ ! -d "$(VCPKG_ROOT)" ]; then \
		echo "Error: vcpkg not found at $(VCPKG_ROOT)"; \
		echo "Run 'make deps' in the parent directory first"; \
		exit 1; \
	fi
	@if [ ! -f "$(VCPKG_TOOLCHAIN)" ]; then \
		echo "Error: vcpkg toolchain not found at $(VCPKG_TOOLCHAIN)"; \
		exit 1; \
	fi
	@echo "✓ vcpkg configuration OK"

configure-debug: check-vcpkg
	@echo "Configuring debug build..."
	@mkdir -p $(BUILD_DIR_DEBUG)
	cd $(BUILD_DIR_DEBUG) && cmake ../.. \
		-DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_TOOLCHAIN) \
		-DVCPKG_INSTALLED_DIR=$(VCPKG_INSTALLED_DIR) \
		$(VCPKG_OVERLAY_TRIPS) \
		-DVCPKG_TARGET_TRIPLET=$(VCPKG_TRIPLET) \
		-DBUILD_TESTS=ON \
		-DUSE_CCACHE=ON \
		-DENABLE_PCH=ON \
		-DCMAKE_C_COMPILER=clang \
		-DCMAKE_CXX_COMPILER=clang++ \
		$(CMAKE_VCPKG_BINARY_SOURCES) \
		$(LINKER_FLAGS) \
		-G $(CMAKE_GENERATOR)
	@echo "✓ Debug build configured"

configure-release: check-vcpkg
	@echo "Configuring release build..."
	@mkdir -p $(BUILD_DIR_RELEASE)
	cd $(BUILD_DIR_RELEASE) && cmake ../.. \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=$(VCPKG_TOOLCHAIN) \
		-DVCPKG_INSTALLED_DIR=$(VCPKG_INSTALLED_DIR) \
		$(VCPKG_OVERLAY_TRIPS) \
		-DVCPKG_TARGET_TRIPLET=$(VCPKG_TRIPLET) \
		-DBUILD_TESTS=ON \
		-DUSE_CCACHE=ON \
		-DENABLE_PCH=ON \
		-DCMAKE_C_COMPILER=clang \
		-DCMAKE_CXX_COMPILER=clang++ \
		$(CMAKE_VCPKG_BINARY_SOURCES) \
		$(LINKER_FLAGS) \
		-G $(CMAKE_GENERATOR)
	@echo "✓ Release build configured"

configure: configure-debug configure-release

build-debug: configure-debug
	@echo "Building debug..."
	cmake --build $(BUILD_DIR_DEBUG) -- -j$(NPROC)
	@echo "✓ Debug build complete"
	@$(MAKE) clangd-helpers

build-release: configure-release
	@echo "Building release..."
	cmake --build $(BUILD_DIR_RELEASE) -- -j$(NPROC)
	@echo "✓ Release build complete"

install: build-release
	@echo "Installing falcon-routine..."
	cmake --install $(BUILD_DIR_RELEASE)
	@echo "✓ Installation complete"

clean:
	@echo "Cleaning build artifacts and test containers..."
	rm -rf $(BUILD_DIR_DEBUG) $(BUILD_DIR_RELEASE) build/ compile_commands.json ./vcpkg_installed/
	@echo "✓ Clean complete"

.PHONY: clangd-helpers
clangd-helpers:
	@if [ -f $(BUILD_DIR_DEBUG)/compile_commands.json ]; then \
		ln -sf $(BUILD_DIR_DEBUG)/compile_commands.json compile_commands.json; \
		echo "✓ clangd compile_commands.json symlinked"; \
	fi

test: build-release
	@cd $(BUILD_DIR_RELEASE) && \
		ctest --output-on-failure --verbose -C Release
	@echo "✓ All tests passed"

test-debug: build-debug
	@cd $(BUILD_DIR_DEBUG) && \
		ctest --output-on-failure --verbose -C Debug
	@echo "✓ All tests passed"

coverage: build-release
	LLVM_PROFILE_FILE=$(BUILD_DIR_RELEASE)/integration_tests.profraw PATH=$(BUILD_DIR_RELEASE):$$PATH $(BUILD_DIR_RELEASE)/tests/integration_tests || true
	LLVM_PROFILE_FILE=$(BUILD_DIR_RELEASE)/unit_tests.profraw PATH=$(BUILD_DIR_RELEASE):$$PATH $(BUILD_DIR_RELEASE)/tests/unit_tests || true
	llvm-profdata merge -sparse $(BUILD_DIR_RELEASE)/*.profraw -o $(BUILD_DIR_RELEASE)/instrument_server_core.profdata
	llvm-cov show $(BUILD_DIR_RELEASE)/libinstrument-script-server-core.so \
		-instr-profile=$(BUILD_DIR_RELEASE)/instrument_server_core.profdata \
		-ignore-filename-regex='(tests/)' \
		-Xdemangler c++filt -Xdemangler -n

coverage-overview:
	@llvm-cov report $(BUILD_DIR_RELEASE)/libinstrument-script-server-core.so \
		-instr-profile=$(BUILD_DIR_RELEASE)/instrument_server_core.profdata \
		-ignore-filename-regex='(tests/)' \
		-Xdemangler c++filt -Xdemangler -n
