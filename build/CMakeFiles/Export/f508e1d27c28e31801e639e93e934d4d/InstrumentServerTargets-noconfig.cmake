#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "InstrumentServer::instrument-server-core" for configuration ""
set_property(TARGET InstrumentServer::instrument-server-core APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(InstrumentServer::instrument-server-core PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libinstrument-server-core.a"
  )

list(APPEND _cmake_import_check_targets InstrumentServer::instrument-server-core )
list(APPEND _cmake_import_check_files_for_InstrumentServer::instrument-server-core "${_IMPORT_PREFIX}/lib/libinstrument-server-core.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
