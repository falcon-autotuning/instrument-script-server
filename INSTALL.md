# Installation Guide

This guide covers detailed installation instructions for various platforms.

## Table of Contents

- [Ubuntu 22.04 LTS](#ubuntu-2204-lts)
- [Arch Linux](#arch-linux)
- [Windows](#windows)
- [Troubleshooting](#troubleshooting)

---

## Ubuntu 22.04 LTS

### Quick Install (Automated)

```bash
# Install all dependencies at once
sudo make setup-ubuntu
```

### Manual Installation

If you prefer to install dependencies manually or the automated script doesn't work:

#### 1. Install System Dependencies

```bash
# Update package list
sudo apt update

# Install build tools and compilers
sudo apt install -y build-essential git cmake ninja-build clang lld

# Install C++ standard library development files
sudo apt install -y libstdc++-12-dev

# Install libraries
sudo apt install -y \
    liblua5.3-dev \
    libspdlog-dev \
    nlohmann-json3-dev \
    libyaml-cpp-dev \
    libboost-all-dev

# Install testing framework
sudo apt install -y libgtest-dev
```

#### 2. Build and Install Google Test

Ubuntu's `libgtest-dev` package only provides source files, so we need to build them:

```bash
# Navigate to GTest source directory
cd /usr/src/googletest/googletest

# Build GTest
sudo cmake .
sudo make

# Copy libraries to system library directory
sudo cp lib/*.a /usr/lib/

# Return to project directory
cd -
```

#### 3. Install sol2 (Lua C++ Bindings)

sol2 is a header-only library that needs to be installed manually:

```bash
# Clone sol2 repository
git clone --depth 1 --branch v3.3.0 https://github.com/ThePhD/sol2.git /tmp/sol2

# Copy headers to system include directory
sudo mkdir -p /usr/local/include
sudo cp -r /tmp/sol2/include/sol /usr/local/include/

# Cleanup
rm -rf /tmp/sol2
```

#### 4. Build the Project

```bash
# Clone the repository (if not already cloned)
git clone https://github.com/falcon-autotuning/instrument-script-server.git
cd instrument-script-server

# Clean any previous builds
make clean

# Build the project
make build
```

#### 5. Install the Server

```bash
# Install binaries, libraries, and documentation
sudo cmake --install build

# Update dynamic linker cache
sudo ldconfig
```

#### 6. Verify Installation

```bash
# Check if the server is installed correctly
instrument-server --help

# Check available plugins
instrument-server plugins
```

### Known Issues on Ubuntu 22.04

#### yaml-cpp Namespace Issue

Ubuntu 22.04's `libyaml-cpp-dev` (version 0.7.0) exports targets without the modern CMake namespace (`yaml-cpp` instead of `yaml-cpp::yaml-cpp`). This is automatically handled by the build system, which creates an alias target.

If you encounter yaml-cpp related errors, verify the fix exists in `CMakeLists.txt`:

```cmake
# Create alias if it doesn't exist (for older yaml-cpp versions)
if(TARGET yaml-cpp AND NOT TARGET yaml-cpp::yaml-cpp)
  add_library(yaml-cpp::yaml-cpp ALIAS yaml-cpp)
endif()
```

---

## Arch Linux

### Quick Install (Automated)

```bash
# Install all dependencies at once
sudo make setup-arch
```

### Manual Installation

```bash
# Install all dependencies
sudo pacman -S base-devel git cmake ninja clang lld llvm \
    lua luajit spdlog nlohmann-json yaml-cpp gtest boost

# Install sol2 (header-only)
git clone --depth 1 --branch v3.3.0 https://github.com/ThePhD/sol2.git /tmp/sol2
sudo mkdir -p /usr/local/include
sudo cp -r /tmp/sol2/include/sol /usr/local/include/
rm -rf /tmp/sol2

# Build and install
git clone https://github.com/falcon-autotuning/instrument-script-server.git
cd instrument-script-server
make clean
make build
sudo make install

# Verify installation
instrument-server --help
```

---

## Windows

Windows builds use vcpkg for dependency management. Dependencies are defined in `vcpkg.json`.

### Prerequisites

1. Install Visual Studio 2019 or later with C++ development tools
2. Install Git for Windows
3. Install CMake (3.20 or later)

### Building with vcpkg

```powershell
# Clone the repository
git clone https://github.com/falcon-autotuning/instrument-script-server.git
cd instrument-script-server

# vcpkg will automatically install dependencies
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release

# Install
cmake --install build
```

---

## Troubleshooting

### "error while loading shared libraries: libinstrument-server-core.so"

After installation, if you see this error, run:

```bash
sudo ldconfig
```

This updates the dynamic linker cache so it can find the newly installed library.

### CMake cannot find GTest

If CMake reports missing GTest, ensure you've built and installed it:

```bash
cd /usr/src/googletest/googletest
sudo cmake .
sudo make
sudo cp lib/*.a /usr/lib/
```

### CMake cannot find yaml-cpp::yaml-cpp

This is a known issue with older yaml-cpp packages. The build system should handle this automatically. If you still have issues, verify that `libyaml-cpp-dev` is installed:

```bash
sudo apt install libyaml-cpp-dev
```

### Missing sol2 Headers

sol2 must be installed manually as it's a header-only library:

```bash
git clone --depth 1 --branch v3.3.0 https://github.com/ThePhD/sol2.git /tmp/sol2
sudo cp -r /tmp/sol2/include/sol /usr/local/include/
rm -rf /tmp/sol2
```

### Boost Interprocess Headers Not Found

Install the complete Boost development package:

```bash
# Ubuntu/Debian
sudo apt install libboost-all-dev

# Arch Linux
sudo pacman -S boost
```

### Linker Errors with lld

Ensure the lld linker is installed:

```bash
# Ubuntu/Debian
sudo apt install lld

# Arch Linux
sudo pacman -S lld
```

---

## Testing Your Installation

After installation, run the test suite to verify everything works:

```bash
cd build

# Run unit tests
./tests/unit_tests

# Run integration tests
./tests/integration_tests

# Optional: Run performance tests
./tests/perf_tests
```

All tests should pass. If any tests fail, check the troubleshooting section or open an issue.

---

## Uninstalling

To remove the instrument-script-server:

```bash
cd /path/to/instrument-script-server
sudo cmake --build build --target uninstall  # If your CMake supports uninstall

# Or manually remove files:
sudo rm -f /usr/local/bin/instrument-server
sudo rm -f /usr/local/bin/instrument-worker
sudo rm -f /usr/local/bin/validate-*
sudo rm -f /usr/local/bin/template-expander
sudo rm -f /usr/local/bin/generate-instrument-config
sudo rm -rf /usr/local/include/instrument-server
sudo rm -rf /usr/local/share/instrument-server
sudo rm -rf /usr/local/share/doc/instrument-server
sudo rm -f /usr/local/lib/libinstrument-server-core.so
sudo rm -rf /usr/local/lib/instrument-plugins
sudo rm -rf /usr/local/lib/cmake/InstrumentServer
sudo ldconfig
```

---

## Development Setup

For active development, you may want to:

1. **Use a symlink instead of installing:**
   ```bash
   # Add build directory to PATH
   export PATH="$PWD/build:$PATH"
   
   # Or create a symlink
   sudo ln -s $PWD/build/instrument-server /usr/local/bin/instrument-server
   ```

2. **Enable debug builds:**
   ```bash
   cd build
   cmake -DCMAKE_BUILD_TYPE=Debug ..
   ninja
   ```

3. **Use ccache for faster rebuilds:**
   ```bash
   sudo apt install ccache  # Ubuntu
   sudo pacman -S ccache    # Arch
   ```

---

## Next Steps

After installation, see:
- [README.md](README.md) for quick start guide
- [CLI_USAGE.md](docs/CLI_USAGE.md) for command-line reference
- [CONFIGURATION.md](docs/CONFIGURATION.md) for configuration file format
- [examples/](examples/) for sample configurations and scripts
