# clang -> mingw-w64 sysroot toolchain for x86_64-w64-mingw32
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
# substituted by Makefile
set(CMAKE_SYSROOT "@MINGW_SYSROOT@")

# Make the sysroot the primary search root
list(APPEND CMAKE_PREFIX_PATH
  "${CMAKE_SYSROOT}/lib/cmake"
  "${CMAKE_SYSROOT}/share/cmake"
  "${CMAKE_SYSROOT}/usr/lib/cmake"
  "${CMAKE_SYSROOT}/usr/share/cmake"
  "${CMAKE_SYSROOT}"
)

# Ensure CMake searches the sysroot for libraries/includes/packages (don't search host)
set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CLANG_TARGET "--target=x86_64-w64-mingw32")
set(CLANG_SYSROOT "--sysroot=${CMAKE_SYSROOT}")

# Insert an optional --gcc-toolchain flag (substituted by Makefile)
# and an optional -L for the detected GCC lib dir
set(CMAKE_C_FLAGS "${CLANG_TARGET} ${CLANG_SYSROOT} @MINGW_GCC_TOOLCHAIN_FLAG@ ${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CLANG_TARGET} ${CLANG_SYSROOT} @MINGW_GCC_TOOLCHAIN_FLAG@ ${CMAKE_CXX_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CLANG_TARGET} ${CLANG_SYSROOT} -fuse-ld=lld @MINGW_GCC_LDFLAGS@ ${CMAKE_EXE_LINKER_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
