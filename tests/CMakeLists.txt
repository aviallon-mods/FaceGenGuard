# Off-game test suite for FaceGenGuard's handler policy core.
#
# Deliberately its own tiny project with no CommonLibSSE-NG and no Windows
# dependency: the core under src/Core is plain C++, and building + RUNNING it on
# Linux as well as Windows is what makes "the handler decides X" a two-toolchain
# claim instead of a one-compiler claim. FG_NO_PCH keeps the shared source off
# the plugin's precompiled header (Windows.h + CommonLibSSE-NG).

cmake_minimum_required(VERSION 3.21)
project(FaceGenGuardTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(fgtests
    main.cpp
    test_resilience.cpp
    ../src/Core/ResiliencePolicy.cpp)

target_include_directories(fgtests PRIVATE .. ../src .)
target_compile_definitions(fgtests PRIVATE FG_NO_PCH)

if(MSVC)
    target_compile_options(fgtests PRIVATE /W4)
else()
    target_compile_options(fgtests PRIVATE -Wall -Wextra -Wpedantic)
    find_package(Threads REQUIRED)
    target_link_libraries(fgtests PRIVATE Threads::Threads)
endif()

enable_testing()
add_test(NAME offgame COMMAND fgtests)
