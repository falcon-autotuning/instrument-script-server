vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/instrument-plugin-api
    REF v${VERSION}
    SHA512 724a1204d33e6a82a265018af16edfad5c2393b5e45f1d8dfe372042d8fbc58a7de4862af6eb4c7414783965d3829b42ee230f652cc7a5bc6d35c53c8b05ff3f
)

if("plugin" IN_LIST FEATURES)
  set(INSTRUMENT_PLUGIN_ENABLE_PLUGIN ON)
endif()

if("host" IN_LIST FEATURES)
  set(INSTRUMENT_PLUGIN_ENABLE_HOST ON)
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)

vcpkg_copy_pdbs()
