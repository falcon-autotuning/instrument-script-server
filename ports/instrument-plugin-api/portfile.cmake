vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/instrument-plugin-api
    REF v${VERSION}
    SHA512 f72c0b014ecc65ed962883a57bb3c72a5fb2888930940301a7e2455af0ee95c4c6e488e6e74f58568a5d19dc282df3f859595cf0ea9d2e8d3c2992d02a452e2f
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
