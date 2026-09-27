# vcpkg triplet for cross-building MSVC-ABI Windows x64 binaries from Linux
# with clang-cl against an xwin sysroot. Same as CommonLibSSE-NG's example
# triplet (examples/linux-cross-compile/custom-triplets), plus release-only
# builds (a debug copy doubles every dependency compile for nothing).
#
# Do not set VCPKG_CMAKE_SYSTEM_NAME to "Windows": vcpkg only sets
# VCPKG_TARGET_IS_WINDOWS when it is undefined or empty.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)

# build.sh exports CLNG_ROOT (a CommonLibSSE-NG checkout at v6.8.0).
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "$ENV{CLNG_ROOT}/cmake/toolchain-linux-clangcl.cmake")
