# Makefile Targets Reference

This document lists all available Makefile targets for the instrument-script-server project.

## Setup & Installation

### `make setup-ubuntu`
Complete setup for Ubuntu 22.04 LTS (installs all dependencies + sol2).
```bash
sudo make setup-ubuntu
```

### `make setup-arch`
Complete setup for Arch Linux (installs all dependencies + sol2).
```bash
sudo make setup-arch
```

### `make install-deps-ubuntu`
Install only system dependencies for Ubuntu (no sol2).
```bash
sudo make install-deps-ubuntu
```

### `make install-deps-arch`
Install only system dependencies for Arch Linux (no sol2).
```bash
sudo make install-deps-arch
```

### `make install-sol2`
Install sol2 header-only library to `/usr/local/include/sol`.
```bash
sudo make install-sol2
```

### `make install`
Install built binaries and libraries to system directories.
```bash
sudo make install
```
**Note:** Automatically runs `ldconfig` to update library cache.

## Building

### `make build` (default)
Build the project using Ninja and Clang with coverage instrumentation.
```bash
make build
```

### `make clean`
Remove build directories.
```bash
make clean
```

## Testing

### `make unit-test`
Run unit tests with coverage profiling.
```bash
make unit-test
```

### `make integration-tests`
Run integration tests with coverage profiling.
```bash
make integration-tests
```

### `make perf-tests`
Run performance benchmarks with coverage profiling.
```bash
make perf-tests
```

## Coverage Analysis

### `make coverage`
Generate full coverage report (runs build + tests).
```bash
make coverage
```

### `make coverage-overview`
Display coverage summary.
```bash
make coverage-overview
```

## Typical Workflows

### First Time Setup (Ubuntu)
```bash
# 1. Install all dependencies
sudo make setup-ubuntu

# 2. Build the project
make build

# 3. Install system-wide
sudo make install

# 4. Verify installation
instrument-script-server --help
```

### First Time Setup (Arch Linux)
```bash
# 1. Install all dependencies
sudo make setup-arch

# 2. Build the project
make build

# 3. Install system-wide
sudo make install

# 4. Verify installation
instrument-script-server --help
```

### Development Iteration
```bash
# Edit code...

# Rebuild
make build

# Run tests
make unit-test
make integration-tests

# Optional: Check coverage
make coverage-overview
```

### Clean Rebuild
```bash
make clean
make build
sudo make install
```

## Environment Variables

You can override these variables:

- `BUILD_DIR`: Build directory (default: `./build-win`)
- `CMAKE`: CMake command (default: `cmake`)
- `NINJA`: Ninja command (default: `ninja`)

Example:
```bash
BUILD_DIR=./my-build make build
```

## Advanced Usage

### Build Without Coverage Instrumentation

Edit the Makefile or run cmake directly:
```bash
mkdir -p build
cd build
CC=clang CXX=clang++ cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja
```

### Install to Custom Prefix

```bash
cd build
sudo cmake --install . --prefix=/opt/instrument-script-server
```

### Run Tests in Debugger

```bash
gdb ./build/tests/unit_tests
# or
lldb ./build/tests/unit_tests
```

## Troubleshooting

### "make: *** No rule to make target"
Ensure you're in the project root directory and the Makefile exists.

### Permission Denied
Some targets require sudo (setup-*, install-*):
```bash
sudo make setup-ubuntu
```

### Build Fails After Updating
Try a clean rebuild:
```bash
make clean
make build
```

## See Also

- [INSTALL.md](INSTALL.md) - Detailed installation guide
- [README.md](README.md) - Project overview and quick start
- [CONTRIBUTING.md](CONTRIBUTING.md) - Development guidelines
