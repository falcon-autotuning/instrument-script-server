#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "instserver::instrument_server_lib" for configuration ""
set_property(TARGET instserver::instrument_server_lib APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(instserver::instrument_server_lib PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libinstrument_server_lib.so"
  IMPORTED_SONAME_NOCONFIG "libinstrument_server_lib.so"
  )

list(APPEND _cmake_import_check_targets instserver::instrument_server_lib )
list(APPEND _cmake_import_check_files_for_instserver::instrument_server_lib "${_IMPORT_PREFIX}/lib/libinstrument_server_lib.so" )

# Import target "instserver::instrument-worker" for configuration ""
set_property(TARGET instserver::instrument-worker APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(instserver::instrument-worker PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/bin/instrument-worker"
  )

list(APPEND _cmake_import_check_targets instserver::instrument-worker )
list(APPEND _cmake_import_check_files_for_instserver::instrument-worker "${_IMPORT_PREFIX}/bin/instrument-worker" )

# Import target "instserver::instrument-server" for configuration ""
set_property(TARGET instserver::instrument-server APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(instserver::instrument-server PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/bin/instrument-server"
  )

list(APPEND _cmake_import_check_targets instserver::instrument-server )
list(APPEND _cmake_import_check_files_for_instserver::instrument-server "${_IMPORT_PREFIX}/bin/instrument-server" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
