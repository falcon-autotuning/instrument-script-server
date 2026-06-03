vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/instrument-data
    REF v${VERSION}
    SHA512 18b19e7920f11f5573d532081699b6806cca4b536f6c755ed9a2ea72191d233049892e6345ecdfbf71123ec9fb4aae946f6c946e2a2a6ef1edb5967083430e34
    HEAD_REF main
)


# Patch manager.c
#
# 1. data_manager_release_buffer() had no init() call, so the process-local
#    mutex 'lock' could be uninitialized on first release.
# 2. Add timestamp tracking and fix the double reference count bug on creation.
file(READ "${SOURCE_PATH}/src/manager.c" MANAGER_C_CONTENT)

string(REPLACE
    "#include \"internal/util.h\""
    "#include \"internal/util.h\"\n\n#ifdef _WIN32\n#include <windows.h>\nstatic uint64_t inst_get_timestamp_ms(void) {\n  FILETIME ft;\n  GetSystemTimeAsFileTime(&ft);\n  ULARGE_INTEGER uli;\n  uli.LowPart = ft.dwLowDateTime;\n  uli.HighPart = ft.dwHighDateTime;\n  return (uli.QuadPart - 116444736000000000ULL) / 10000ULL;\n}\n#else\n#include <time.h>\nstatic uint64_t inst_get_timestamp_ms(void) {\n  struct timespec ts;\n  clock_gettime(CLOCK_REALTIME, &ts);\n  return (uint64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);\n}\n#endif"
    MANAGER_C_CONTENT "${MANAGER_C_CONTENT}")

string(REPLACE
    "meta->timestamp_ms = 0;"
    "meta->timestamp_ms = inst_get_timestamp_ms();"
    MANAGER_C_CONTENT "${MANAGER_C_CONTENT}")

string(REPLACE
    "  DataBuffer *buffer = _get_buffer_internal(safe_id);\n  if (buffer)\n    data_buffer_ref(buffer);"
    "  DataBuffer *buffer = lookup_buffer_no_lock(safe_id);"
    MANAGER_C_CONTENT "${MANAGER_C_CONTENT}")

string(APPEND MANAGER_C_CONTENT "\n\nvoid inst_map_remove(const char *id) {\n  init();\n  mtx_lock(&lock);\n  MapEntry *e;\n  HASH_FIND_STR(map, id, e);\n  if (e) {\n    HASH_DEL(map, e);\n    free(e->id);\n    free(e);\n  }\n  mtx_unlock(&lock);\n}\n")

file(WRITE "${SOURCE_PATH}/src/manager.c" "${MANAGER_C_CONTENT}")

# Patch buffer.c to remove from map when process-local ref count drops to 0
file(READ "${SOURCE_PATH}/src/buffer.c" BUFFER_C_CONTENT)
string(REPLACE
    "if (atomic_fetch_sub(&buffer->ref_count, 1) == 1) {"
    "if (atomic_fetch_sub(&buffer->ref_count, 1) == 1) {\n    void inst_map_remove(const char *id);\n    inst_map_remove(buffer->id);"
    BUFFER_C_CONTENT "${BUFFER_C_CONTENT}")
file(WRITE "${SOURCE_PATH}/src/buffer.c" "${BUFFER_C_CONTENT}")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTS=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_copy_pdbs()

set(VCPKG_POLICY_SKIP_ABSOLUTE_PATHS_CHECK enabled)
