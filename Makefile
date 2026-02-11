# Simple Makefile for local builds (Linux + cross-clang-to-windows)
# Use: 
#   make build            # cross-build for Windows using clang/clang++
#   make clean            # clean build directories
# Optional overrides: 
#   MINGW_SYSROOT=/path/to/sysroot   (defaults to /usr/x86_64-w64-mingw32)
#   BUILD_DIR=./build-win             (defaults to ./build-win for Windows builds)

.PHONY: all build clean unit-test integration-tests perf-tests coverage \
        build-native build-windows wine-unit-test wine-integration-test \
        install-deps-ubuntu install-deps-arch install-sol2 setup-ubuntu setup-arch \
        install

BUILD_DIR ?= ./build-win
CMAKE ?= cmake
NINJA ?= ninja

all: build

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
	LLVM_PROFILE_FILE=./build/perf_tests.profraw PATH=./build:$$PATH ./build/tests/perf_tests

coverage: build unit-test integration-tests
	llvm-profdata merge -sparse ./build/*.profraw -o ./build/instrument_server_core.profdata
	llvm-cov show ./build/libinstrument-server-core.so \
		-instr-profile=./build/instrument_server_core.profdata \
		-ignore-filename-regex='(tests/)' \
		-Xdemangler c++filt -Xdemangler -n

coverage-overview: 
	@llvm-cov report ./build/libinstrument-server-core.so \
		-instr-profile=./build/instrument_server_core.profdata \
		-ignore-filename-regex='(tests/)' \
		-Xdemangler c++filt -Xdemangler -n
# ============================================================================
# Dependency Installation Targets
# ============================================================================

# Install sol2 header-only library
install-sol2:
	@echo "Installing sol2 (Lua C++ bindings)..."
	@if [ -d "/usr/local/include/sol" ]; then \
		echo "sol2 already installed at /usr/local/include/sol"; \
	else \
		git clone --depth 1 --branch v3.3.0 https://github.com/ThePhD/sol2.git /tmp/sol2 && \
		sudo mkdir -p /usr/local/include && \
		sudo cp -r /tmp/sol2/include/sol /usr/local/include/ && \
		rm -rf /tmp/sol2 && \
		echo "sol2 installed successfully"; \
	fi

# Ubuntu 22.04 LTS dependency installation
install-deps-ubuntu:
	@echo "Installing dependencies for Ubuntu 22.04..."
	sudo apt update
	sudo apt install -y build-essential git cmake ninja-build clang lld
	sudo apt install -y libstdc++-12-dev
	sudo apt install -y liblua5.3-dev libspdlog-dev nlohmann-json3-dev libyaml-cpp-dev libboost-all-dev
	sudo apt install -y libgtest-dev
	@echo "Building Google Test from source..."
	cd /usr/src/googletest/googletest && sudo cmake . && sudo make && sudo cp lib/*.a /usr/lib/
	@echo "Dependencies installed successfully!"

# Arch Linux dependency installation
install-deps-arch:
	@echo "Installing dependencies for Arch Linux..."
	sudo pacman -Sy --needed --noconfirm base-devel git cmake ninja clang lld llvm \
		lua luajit spdlog nlohmann-json yaml-cpp gtest boost
	@echo "Dependencies installed successfully!"

# Complete Ubuntu setup (dependencies + sol2)
setup-ubuntu: install-deps-ubuntu install-sol2
	@echo "Ubuntu setup complete! Ready to build."
	@echo "Run 'make build' to compile the project."

# Complete Arch setup (dependencies + sol2)
setup-arch: install-deps-arch install-sol2
	@echo "Arch Linux setup complete! Ready to build."
	@echo "Run 'make build' to compile the project."

# Install the built binaries and libraries
install:
	@echo "Installing instrument-script-server..."
	@if [ ! -d "./build" ]; then \
		echo "Error: Build directory not found. Run 'make build' first."; \
		exit 1; \
	fi
	sudo cmake --install ./build
	sudo ldconfig
	@echo "Installation complete!"
	@echo "Verify with: instrument-server --help"