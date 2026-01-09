# GitHub Workflows

This directory contains CI/CD workflows for the Instrument Script Server project.

## Workflows

### 1. Arch Linux Build and Test (`arch-linux.yml`)

**Trigger:** Push to main, pull requests, manual dispatch, or called by other workflows

**Purpose:** Build and test the project on Arch Linux

**Steps:**
1. Sets up Arch Linux container on Ubuntu runner
2. Installs all required dependencies (clang, cmake, ninja, lua, etc.)
3. Runs `make clean` to ensure clean build
4. Runs `make build` to compile the project
5. Runs `make unit-test` to execute unit tests
6. Runs `make integration-tests` to execute integration tests
7. Uploads build artifacts (binaries + LICENSE)

**Optimizations:**
- Caches Pacman packages to speed up dependency installation

### 2. Windows Build and Test (`windows.yml`)

**Trigger:** Push to main, pull requests, manual dispatch, or called by other workflows

**Purpose:** Cross-compile for Windows and test natively on Windows

**Jobs:**

#### `cross-compile`
- Uses Arch Linux container with MinGW-w64 toolchain
- Cross-compiles binaries for Windows x64
- Uploads cross-compiled binaries for release

#### `test-on-windows`
- Runs on native Windows with MSYS2
- Builds and tests natively to ensure all tests pass
- Uploads test artifacts (temporary, 1 day retention)

#### `package-windows`
- Depends on both cross-compile and test-on-windows
- Packages the cross-compiled binaries with LICENSE
- Only runs if tests pass
- Uploads final Windows package for release

**Optimizations:**
- Caches Pacman packages for MinGW build
- Caches MSYS2 packages for native Windows build

### 3. Release (`release.yml`)

**Trigger:** Push to main, tags starting with 'v*', or manual dispatch

**Purpose:** Create GitHub releases with compiled binaries for all platforms

**Dependencies:**
- Calls `arch-linux.yml` workflow
- Calls `windows.yml` workflow
- Only proceeds if both workflows succeed

**Steps:**
1. Downloads Arch Linux binaries artifact
2. Downloads Windows binaries artifact
3. Creates platform-specific archives:
   - `instrument-server-arch-linux-x64.tar.gz`
   - `instrument-server-windows-x64.zip`
4. Generates release notes with installation instructions
5. Creates GitHub release with:
   - Platform archives
   - LICENSE file
   - Installation instructions

**Release Behavior:**
- Tags starting with 'v*': Creates proper release
- Push to main: Creates draft/prerelease for testing
- Manual dispatch: Creates release based on current ref

## Artifacts

All workflows upload artifacts that are accessible from the GitHub Actions UI:

- **arch-linux-binaries**: Compiled binaries for Arch Linux (7 days retention)
- **windows-binaries**: Final packaged Windows binaries (7 days retention)
- **windows-native-binaries**: Native Windows test binaries (1 day retention)
- **windows-cross-compiled-binaries**: Cross-compiled Windows binaries (7 days retention)

## Testing Strategy

The workflows ensure quality through:

1. **Arch Linux**: Native build and test using Makefile targets
2. **Windows**: Cross-compilation for release + native build/test for validation
3. **Release**: Only creates releases if all platform tests pass

## Dependencies

### Arch Linux
- base-devel, git, cmake, ninja
- clang, lld, llvm
- lua, luajit
- spdlog, nlohmann-json, yaml-cpp
- gtest, sol2

### Windows (MSYS2/MinGW64)
- cmake, ninja, gcc
- lua, luajit
- spdlog, nlohmann-json, yaml-cpp
- gtest

## Caching

Workflows cache dependency packages to improve build times:

- **Arch Linux**: `/var/cache/pacman/pkg`
- **Windows**: MSYS2 package cache and mingw64 directory

Cache keys are based on workflow file hashes to invalidate when dependencies change.

## Future Improvements

Potential enhancements:
- Add macOS build workflow
- Implement code coverage reporting
- Add static analysis (cppcheck, clang-tidy)
- Create Docker images as artifacts
- Add performance regression testing
