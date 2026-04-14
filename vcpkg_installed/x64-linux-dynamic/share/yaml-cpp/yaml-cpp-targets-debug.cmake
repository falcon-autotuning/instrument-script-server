#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "yaml-cpp::yaml-cpp" for configuration "Debug"
set_property(TARGET yaml-cpp::yaml-cpp APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(yaml-cpp::yaml-cpp PROPERTIES
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/debug/lib/libyaml-cppd.so.0.9.0"
  IMPORTED_SONAME_DEBUG "libyaml-cppd.so.0.9"
  )

list(APPEND _cmake_import_check_targets yaml-cpp::yaml-cpp )
list(APPEND _cmake_import_check_files_for_yaml-cpp::yaml-cpp "${_IMPORT_PREFIX}/debug/lib/libyaml-cppd.so.0.9.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
