vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/instrument-plugin-api
    REF v${VERSION}
    SHA512 2e9b23ce6538654d3fc4d493db96041698d2b751029d8336d7b1f43ae73d7a8e3ba517c0acd2a7b25853a619844272b2ad860316788095936468c94a88761712
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
