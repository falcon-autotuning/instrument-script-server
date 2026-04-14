
.PHONY: all configure build-debug build-release test clean install help check-vcpkg

PLATFORM := linux
CMAKE_GENERATOR := Ninja
VCPKG_TRIPLET ?= x64-linux-dynamic
NPROC := $(shell nproc 2>/dev/null || echo 4)
export CC=clang
export CXX=clang++

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
VCPKG_BINARY_SOURCES ?= ""
ifeq ($(strip $(FEED_URL)),)
  CMAKE_VCPKG_BINARY_SOURCES :=
else
	VCPKG_BINARY_SOURCES := "clear;nuget,$(FEED_URL),readwrite"
  CMAKE_VCPKG_BINARY_SOURCES := -DVCPKG_BINARY_SOURCES=$(VCPKG_BINARY_SOURCES)
endif
LINKER_FLAGS ?=
ifeq ($(PLATFORM),linux)
	LINKER_FLAGS := -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=lld" -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=lld"
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
	@if [ ! -f "$(VCPKG_ROOT)/vcpkg" ]; then \
		echo "Bootstrapping vcpkg..."; \
		cd $(VCPKG_ROOT) && ./bootstrap-vcpkg.sh; \
	fi

setup-nuget-auth:
	@if [ -z "$$NUGET_API_KEY" ]; then \
		echo "No NUGET_API_KEY found, skipping NuGet setup (local-only build, no binary cache)."; \
		exit 0; \
	fi
	@echo "Setting up NuGet authentication for vcpkg binary caching..."
	@if ! command -v mono >/dev/null 2>&1; then \
		echo "Error: mono is not installed. Please install mono (e.g., 'sudo pacman -S mono' on Arch, 'sudo apt install mono-complete' on Ubuntu)."; \
		exit 1; \
	fi
	@NUGET_EXE=$$(vcpkg fetch nuget | tail -n1); \
	mono "$$NUGET_EXE" sources remove -Name "$(FEED_NAME)" || true; \
	mono "$$NUGET_EXE" sources add -Name "$(FEED_NAME)" -Source "$(FEED_URL)" -Username "$(USERNAME)" -Password "$(NUGET_API_KEY)"

.PHONY: vcpkg-install-deps
vcpkg-install-deps: setup-nuget-auth 
	@echo "Installing vcpkg dependencies" 
	VCPKG_FEATURE_FLAGS=binarycaching \
		$(VCPKG_ROOT)/vcpkg install \
		--binarysource=$(VCPKG_BINARY_SOURCES) \
		--triplet="$(VCPKG_TRIPLET)"

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

test-local-debug: build-debug
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
