vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO falcon-autotuning/instrument-plugin-api
    REF v${VERSION}
    SHA512 430332f5f5c5571658465f3b664e7d16e58092d62195f213d080b2a98694badc353d59949a80a90125ddf682a5e5513a48eda9f4d1edd929493a65e8ddc1eb4f
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
