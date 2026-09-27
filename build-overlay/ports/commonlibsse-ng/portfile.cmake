# CommonLibSSE-NG 6.8.0 from source, for the x64-windows-clangcl cross triplet.
#
# Two upstream defects are patched at install time (see below): the exported
# CMake package never ships cmake/CommonLibSSE.cmake (the add_commonlibsse_plugin
# helper every consumer needs), and CommonLibSSEConfig.cmake references the
# Microsoft::DirectXTK imported target without find_dependency()-ing it.
#
# REX_OPTION_{INI,JSON,TOML} are OFF: src/REX/REX.cpp defines its templates with
# `template <class T> void SettingLoad<T>(...)`, a partial specialization that
# MSVC accepts as an extension and clang-cl rejects. Nothing in the Mantella
# plugins uses REX::INI/JSON/TOML (they vendor their own ini.h/json.h), so the
# feature is simply not offered.

set(VCPKG_BUILD_TYPE release)  # debug copy would double a ~16 min compile

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO alandtse/CommonLibVR
    REF 44dd911486bc43b05b55a23781c7e471eef86542
    SHA512 5cb997388060039eb6e326939d3d17aaba4fc9d065e0642df38cdb2c8afd1ee9e4d3877b39de1e4f45c2685c6a58cd1047894a9687207793e64c63dd629202ec
    HEAD_REF main
)

# CLNG's patch-safety feature (SKSE_SUPPORT_PATCH_SAFETY) vendors MinHook's
# 3-file hde64 decoder through FetchContent, but vcpkg builds ports with
# FETCHCONTENT_FULLY_DISCONNECTED=ON (ports must declare their deps), so the
# fetch never happens and target_sources() finds no hde64.c. Pre-fetch MinHook
# here and hand the tree to FetchContent instead.
vcpkg_from_github(
    OUT_SOURCE_PATH MINHOOK_SOURCE_PATH
    REPO TsudaKageyu/minhook
    REF v1.3.4
    SHA512 8a33233598b56ad9da44d22d470c2432f68364dac31bc719fcd6b085e681fa10ddd41865fbde056ee7f4e7a075cc135344b6bf444eadbd7e7314ee1bedfd89b5
    HEAD_REF master
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTS=OFF
        -DENABLE_SKYRIM_SE=ON
        -DENABLE_SKYRIM_AE=ON
        -DENABLE_SKYRIM_VR=OFF
        -DSKSE_SUPPORT_XBYAK=ON
        -DSKSE_SUPPORT_PATCH_SAFETY=ON
        -DREX_OPTION_INI=OFF
        -DREX_OPTION_JSON=OFF
        -DREX_OPTION_TOML=OFF
        -DCOMMONLIB_PREBUILT=OFF
        "-DFETCHCONTENT_SOURCE_DIR_HDE64=${MINHOOK_SOURCE_PATH}"
    MAYBE_UNUSED_VARIABLES
        COMMONLIB_PREBUILT
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME CommonLibSSE CONFIG_PATH lib/cmake/CommonLibSSE)

# Defect 1: add_commonlibsse_plugin() lives in cmake/CommonLibSSE.cmake, which
# the install() rules never install (config.cmake.in expects it beside the
# config file). Ship it.
file(INSTALL "${SOURCE_PATH}/cmake/CommonLibSSE.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE")

# Defect 2: the exported targets reference Microsoft::DirectXTK but the config
# only find_dependency()s spdlog -> consumers fail at generate time.
file(APPEND "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSEConfig.cmake"
    "include(CMakeFindDependencyMacro)\nfind_dependency(directxtk CONFIG)\n")

file(INSTALL "${SOURCE_PATH}/COPYING"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
    RENAME copyright)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
