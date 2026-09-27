# FaceGenGuard - an SKSE plugin that survives one known Skyrim crash instead of
# dying. Built with CommonLibSSE-NG (vcpkg port); see tools/build.sh for the
# Linux -> MSVC-ABI cross build and tests/ for the off-game suite.
#
# The plugin is intentionally tiny and SELF-CONTAINED: one opt-in feature, no
# hooks, no allocators, no other dependency. [Resilience] bEnabled=0 by default
# because the feature CHANGES GAME SEMANTICS (see README).

cmake_minimum_required(VERSION 3.21)

project(FaceGenGuard VERSION 0.1.0 LANGUAGES CXX)

# If you're not using a mod manager, the SKSE plugin can go straight into the
# Skyrim "Data" folder (SKYRIM_FOLDER), or into a mod manager's mods folder
# (SKYRIM_MODS_FOLDER) - same convention as the CommonLibSSE-NG template.
if(DEFINED ENV{SKYRIM_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_FOLDER}/Data")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_FOLDER}/Data")
endif()
if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
endif()

find_package(CommonLibSSE CONFIG REQUIRED)

add_commonlibsse_plugin(${PROJECT_NAME}
    NAME ${PROJECT_NAME}
    AUTHOR "aviallon"
    SOURCES
        src/main.cpp
        src/Config.cpp
        src/ResilienceGuard.cpp
        src/Core/ResiliencePolicy.cpp)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE PCH.h)
target_include_directories(${PROJECT_NAME} PRIVATE . src)
target_compile_definitions(${PROJECT_NAME} PRIVATE FG_VERSION="${PROJECT_VERSION}")

if(DEFINED OUTPUT_FOLDER)
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")
    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")
    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM)
endif()
