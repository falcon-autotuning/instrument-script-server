vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/instrument-data
    REF v${VERSION}
    SHA512 0bd300289c255fff2757933f2c8089827dab57915a949f6784631ecfa7da68d4c62cfb40e471034eacc58003931de8ba7669a07b360ac9f13e968c14c27602ff
    HEAD_REF main
)

# Create a stub embed_bundle.cmake to fix a missing file in the upstream release
file(WRITE "${SOURCE_PATH}/cmake/embed_bundle.cmake" "# Stub embed_bundle.cmake\n")

# ---------------------------------------------------------------------------
# Patch manager.c
#
# Three bugs are fixed:
#
# 1. data_manager_release_buffer() had no init() call, so the process-local
#    hashtable could be NULL on first release.
#
# 2. The original release logic unconditionally locked the IPC mutex and
#    removed this PID from the global owners list, then unlinked shm if the
#    count dropped to zero.  The problem: when the local process-refcount is
#    still > 1 (another C++ DataBuffer wrapper holds a reference), we must
#    NOT yet touch the global metadata.  We gate the global cleanup on
#    is_last_local (local ref_count <= 1).
#
# 3. data_manager_list_buffers() called registry_list() which returns IDs
#    from ALL processes.  clear_all() on that list would release (and unlink)
#    buffers owned by other processes.  We replace the implementation to
#    iterate only the process-local GHashTable.
# ---------------------------------------------------------------------------
file(READ "${SOURCE_PATH}/src/manager.c" MANAGER_C_CONTENT)

# Fix 1 – add init() to release_buffer
string(REPLACE
    "void data_manager_release_buffer(const gchar *id) {"
    "void data_manager_release_buffer(const gchar *id) {\n  init();"
    MANAGER_C_CONTENT "${MANAGER_C_CONTENT}")

# Fix 2 – replace the entire lock/remove-pid/unlock/unref block inside
#          release_buffer with a version guarded by is_last_local.
#          NOTE: the target string must match the upstream source EXACTLY
#          (including blank lines) so the replacement is unambiguous.
string(REPLACE
    "  inst_ipc_mutex_lock(buffer->mutex);\n\n  guint32 pid = inst_get_pid();\n\n  /* remove this process */\n  guint32 new_count = 0;\n\n  for (guint32 i = 0; i < buffer->meta->global_ref_count; i++) {\n    if (buffer->meta->owners[i] != pid) {\n      buffer->meta->owners[new_count++] = buffer->meta->owners[i];\n    }\n  }\n\n  buffer->meta->global_ref_count = new_count;\n\n  gboolean last = (new_count == 0);\n\n  if (last) {\n    registry_remove(id);\n    inst_shm_unlink_name(buffer->shm_data.name);\n    inst_shm_unlink_name(buffer->shm_meta.name);\n  }\n\n  inst_ipc_mutex_unlock(buffer->mutex);\n\n  data_buffer_unref(buffer);\n}"
    "  /* Only touch global metadata when the local ref-count drops to 1. */\n  gboolean is_last_local = (g_atomic_int_get(&buffer->ref_count) <= 1);\n  gboolean last = FALSE;\n\n  if (is_last_local) {\n    inst_ipc_mutex_lock(buffer->mutex);\n\n    guint32 pid = inst_get_pid();\n    guint32 new_count = 0;\n\n    for (guint32 i = 0; i < buffer->meta->global_ref_count; i++) {\n      if (buffer->meta->owners[i] != pid) {\n        buffer->meta->owners[new_count++] = buffer->meta->owners[i];\n      }\n    }\n    buffer->meta->global_ref_count = new_count;\n    last = (new_count == 0);\n\n    if (last) {\n      registry_remove(id);\n      inst_shm_unlink_name(buffer->shm_data.name);\n      inst_shm_unlink_name(buffer->shm_meta.name);\n      g_mutex_lock(\&lock);\n      g_hash_table_remove(map, id);\n      g_mutex_unlock(\&lock);\n    }\n\n    inst_ipc_mutex_unlock(buffer->mutex);\n  }\n\n  data_buffer_unref(buffer);\n}"
    MANAGER_C_CONTENT "${MANAGER_C_CONTENT}")

# Fix 3 – replace list_buffers to enumerate only the process-local map.
string(REPLACE
    "gchar **data_manager_list_buffers(size_t *count) {\n  gchar **list = NULL;\n  registry_list(&list, count);\n  return list;\n}"
    "static void _collect_local_key(gpointer key, gpointer value, gpointer user_data) {\n  GPtrArray *arr = (GPtrArray *)user_data;\n  g_ptr_array_add(arr, g_strdup((gchar *)key));\n}\ngchar **data_manager_list_buffers(size_t *count) {\n  init();\n  if (!count) return NULL;\n  g_mutex_lock(\&lock);\n  GPtrArray *arr = g_ptr_array_new();\n  g_hash_table_foreach(map, _collect_local_key, arr);\n  g_mutex_unlock(\&lock);\n  *count = arr->len;\n  g_ptr_array_add(arr, NULL);\n  return (gchar **)g_ptr_array_free(arr, FALSE);\n}"
    MANAGER_C_CONTENT "${MANAGER_C_CONTENT}")

file(WRITE "${SOURCE_PATH}/src/manager.c" "${MANAGER_C_CONTENT}")

# ---------------------------------------------------------------------------
# Patch instrument-data.h
#
# Expose data_buffer_ref / data_buffer_unref in the public header so the
# C++ wrapper (DataBufferManager.cpp) can balance the ref count after calling
# data_manager_get_buffer() for a non-owning peek.
#
# Also add data_manager_total_local_memory() which sums only process-local
# buffers (not the global registry), so unit-test assertions of
# total_memory_usage() == 0 after clear_all() work reliably.
# ---------------------------------------------------------------------------
file(READ "${SOURCE_PATH}/include/instrument-data.h" HEADER_CONTENT)

string(REPLACE
    "INSTRUMENT_DATA_EXPORT void *data_buffer_data(DataBuffer *buffer);"
    "INSTRUMENT_DATA_EXPORT void *data_buffer_data(DataBuffer *buffer);\nINSTRUMENT_DATA_EXPORT DataBuffer *data_buffer_ref(DataBuffer *buffer);\nINSTRUMENT_DATA_EXPORT void data_buffer_unref(DataBuffer *buffer);"
    HEADER_CONTENT "${HEADER_CONTENT}")

string(REPLACE
    "INSTRUMENT_DATA_EXPORT size_t data_manager_total_memory_usage(void);"
    "INSTRUMENT_DATA_EXPORT size_t data_manager_total_memory_usage(void);\nINSTRUMENT_DATA_EXPORT size_t data_manager_total_local_memory(void);"
    HEADER_CONTENT "${HEADER_CONTENT}")

file(WRITE "${SOURCE_PATH}/include/instrument-data.h" "${HEADER_CONTENT}")

# Add data_manager_total_local_memory() implementation to manager.c (after write above)
file(READ "${SOURCE_PATH}/src/manager.c" MANAGER_C_CONTENT2)
string(REPLACE
    "size_t data_manager_total_memory_usage(void) { return registry_total_memory(); }"
    "size_t data_manager_total_memory_usage(void) { return registry_total_memory(); }\n\nstatic void _sum_local_mem(gpointer key, gpointer value, gpointer user_data) {\n  (void)key;\n  DataBuffer *b = (DataBuffer *)value;\n  size_t *total = (size_t *)user_data;\n  if (b && b->meta) {\n    *total += b->meta->byte_size;\n  }\n}\nsize_t data_manager_total_local_memory(void) {\n  init();\n  g_mutex_lock(&lock);\n  size_t total = 0;\n  g_hash_table_foreach(map, _sum_local_mem, &total);\n  g_mutex_unlock(&lock);\n  return total;\n}"
    MANAGER_C_CONTENT2 "${MANAGER_C_CONTENT2}")
file(WRITE "${SOURCE_PATH}/src/manager.c" "${MANAGER_C_CONTENT2}")

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
