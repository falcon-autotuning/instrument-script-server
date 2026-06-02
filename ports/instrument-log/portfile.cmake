vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/instrument-log
    REF v${VERSION}
    SHA512 0f6ffea99bbb40a0beb1c884eb76bca5403f96f3be4b8dc44399f7debf139983da980da149aae8f38fc36115ce7989769f1ba36114640a5521e24ac809f9493f
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

file(INSTALL "${SOURCE_PATH}/LICENSE"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
     RENAME copyright)

vcpkg_copy_pdbs()
