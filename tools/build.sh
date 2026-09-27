#!/usr/bin/env bash
# Cross-build FaceGenGuard to a Windows x64 DLL (MSVC ABI) on Linux/NixOS, for
# Skyrim AE 1.7.104 (Address Library V5). No Wine for compiling, no MSVC, no
# vcpkg Windows install: clang-cl + lld-link + xwin sysroot + vcpkg (clangcl
# triplet). This is the Mantella-AE104 toolchain, mirrored verbatim (same
# overlay port, same triplet, same chainloaded CLNG toolchain file, same
# pinned CLNG v6.8.0 -- older CLNG cannot read the 1.7.104 versionlib bins).
#
# Just run it: the script re-execs itself inside `nix shell` so every tool
# comes from an expression (never a raw /nix/store path). nixpkgs#stdenv.cc is
# in the list because vcpkg's *host* compiler detection needs a working cc/c++
# (a ~/.nix-profile c++ without its gcc runtime fails on `cannot find Scrt1.o`).
#
#   tools/build.sh [plugin-src-dir]     # defaults to this repo
#
# One-time setup (see tools/setup-toolchain.sh, which does exactly this):
#   - xwin sysroot      : /tmp/xwin-winsysroot (winsysroot-style splat)
#   - vcpkg clone       : ~/.local/share/vcpkg
#   - llvm-mingw        : ~/.local/share/llvm-mingw (builds the fxc2 shader
#                         stand-in used by the overlay directxtk port)
#   - CommonLibSSE-NG   : /tmp/clng (cloned automatically, v6.8.0)
#
# Overridable env: WORK= CLNG= CLNG_ROOT= VCPKG_ROOT= XWIN_SYSROOT=
#                  LLVM_MINGW_BIN= JOBS= VCPKG_DEFAULT_BINARY_CACHE=
set -euo pipefail

if [ -z "${FACEGENGUARD_BUILD_IN_NIX:-}" ]; then
    # nixpkgs#wine64, NOT nixpkgs#wine: the default `wine` package is a
    # 32-bit-only build and fails on the 64-bit fxc2.exe with "Bad EXE format".
    exec nix shell nixpkgs#vcpkg nixpkgs#cmake nixpkgs#ninja nixpkgs#wine64 \
        nixpkgs#git nixpkgs#curl nixpkgs#patch nixpkgs#llvm nixpkgs#file \
        nixpkgs#stdenv.cc nixpkgs#llvmPackages.clang-unwrapped nixpkgs#lld \
        nixpkgs#pkg-config \
        -c env FACEGENGUARD_BUILD_IN_NIX=1 "$0" "$@"
fi

REPO=$(cd "$(dirname "$0")/.." && pwd)
SRC_IN=${1:-$REPO}
NAME=$(basename "$(realpath "$SRC_IN")")

WORK=${WORK:-/tmp/facegenguard-build}
CLNG=${CLNG:-/tmp/clng}
CLNG_ROOT=${CLNG_ROOT:-$CLNG}
VCPKG_ROOT=${VCPKG_ROOT:-$HOME/.local/share/vcpkg}
XWIN_SYSROOT=${XWIN_SYSROOT:-/tmp/xwin-winsysroot}
LLVM_MINGW_BIN=${LLVM_MINGW_BIN:-$HOME/.local/share/llvm-mingw/bin}
JOBS=${JOBS:-$(nproc)}
CLNG_PIN=44dd911486bc43b05b55a23781c7e471eef86542  # v6.8.0, has Address Library V5

export CLNG_ROOT XWIN_SYSROOT LLVM_MINGW_BIN
export VCPKG_ROOT
export VCPKG_KEEP_ENV_VARS="CLNG_ROOT XWIN_SYSROOT LLVM_MINGW_BIN"
export VCPKG_DEFAULT_BINARY_CACHE=${VCPKG_DEFAULT_BINARY_CACHE:-$HOME/.cache/vcpkg-binaries}
export WINEDEBUG=-all
export WINEPREFIX=${WINEPREFIX:-$WORK/wineprefix}
# Fresh prefix every run: a prefix created by a 32-bit wine is rejected by the
# 64-bit build (and vice versa), and prefix creation costs only seconds.
rm -rf "$WINEPREFIX"

[ -d "$XWIN_SYSROOT" ] || { echo "FATAL: no sysroot at $XWIN_SYSROOT (run tools/setup-toolchain.sh)"; exit 1; }
[ -d "$VCPKG_ROOT/ports" ] || { echo "FATAL: no vcpkg clone at $VCPKG_ROOT (run tools/setup-toolchain.sh)"; exit 1; }
[ -x "$LLVM_MINGW_BIN/x86_64-w64-mingw32-clang++" ] || { echo "FATAL: no llvm-mingw at $LLVM_MINGW_BIN (run tools/setup-toolchain.sh)"; exit 1; }

# CLNG checkout: source of the chainloaded toolchain file and of the overlay
# directxtk port (Wine/fxc2 shader-compile workaround).
if [ ! -d "$CLNG/.git" ]; then
    git clone https://github.com/alandtse/CommonLibVR "$CLNG"
fi
git -C "$CLNG" checkout -q "$CLNG_PIN"

SRC=$WORK/$NAME-src
BUILD=$WORK/$NAME-build
rm -rf "$SRC" "$BUILD"
mkdir -p "$SRC" "$WORK" "$VCPKG_DEFAULT_BINARY_CACHE"
cp -a "$SRC_IN"/. "$SRC"/
rm -rf "$SRC/.git"

# The plugin's committed vcpkg-configuration.json is already the recent
# microsoft/vcpkg baseline; this rewrite is kept so a stale local checkout
# cannot silently pin a 2022 registry (the defect that broke Address Library
# V5 for every upstream Mantella plugin).
cat > "$SRC/vcpkg-configuration.json" <<'EOF'
{
    "default-registry": {
        "kind": "git",
        "repository": "https://github.com/microsoft/vcpkg.git",
        "baseline": "07f4812200df3d3c931c0c8a6081d3b21fe2bf9f"
    }
}
EOF

# Per-source fixups, kept as reviewable patches (none today).
for p in "$REPO"/patches/"$NAME"-*.patch; do
    [ -e "$p" ] || continue
    echo "== applying $(basename "$p")"
    patch -d "$SRC" -p1 < "$p"
done

cmake -S "$SRC" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=x64-windows-clangcl \
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$CLNG/cmake/toolchain-linux-clangcl.cmake" \
    -DVCPKG_OVERLAY_TRIPLETS="$REPO/build-overlay/triplets;$CLNG/examples/linux-cross-compile/custom-triplets" \
    -DVCPKG_OVERLAY_PORTS="$REPO/build-overlay/ports;$CLNG/examples/linux-cross-compile/custom-ports" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_C_FLAGS="/DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS" \
    -DCMAKE_CXX_FLAGS="/DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS"

cmake --build "$BUILD" --parallel "$JOBS"

# ---- Verify: "it compiled" is not "it works". ----
DLL=$(find "$BUILD" -maxdepth 2 -name '*.dll' | head -1)
[ -n "$DLL" ] || { echo "FATAL: no DLL produced in $BUILD"; exit 1; }
echo "== built: $DLL"
sha256sum "$DLL"

OUT=$WORK/out
mkdir -p "$OUT"
cp "$DLL" "$OUT/$(basename "$DLL")"

echo "== PE identity"
llvm-objdump -p "$DLL" | grep -E 'DLL name|DLL magic|Subsystem' || true
echo "== exports (expect SKSEPlugin_Load/Query/Version)"
llvm-objdump -p "$DLL" | sed -n '/Export Table/,/^$/p' | grep -E 'SKSEPlugin|Name' || true

echo "== feature strings (the guard must actually be compiled in)"
# Dump first, then grep the dump: `strings | grep -q` under `set -o pipefail`
# reports failure when grep -q exits early and strings dies of SIGPIPE (141)
# EVEN ON A MATCH -- a false negative from the check itself.
strings -a "$DLL" > "$WORK/dll-strings.txt"
for needle in "FaceGenGuard" "resilience-guard" "behaviourChanging=resilience-guard" \
              "behaviourChanging=none" "sRvaWhitelist" "uMaxSuppressions" \
              "FaceGenGuard-resilience.log"; do
    grep -qF "$needle" "$WORK/dll-strings.txt" || { echo "FATAL: $needle missing from the DLL"; exit 1; }
done
echo "== all feature strings present"

echo "== structured verification"
python3 "$REPO/tools/verify-dll.py" "$DLL"
