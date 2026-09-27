#!/usr/bin/env bash
# One-time toolchain setup for tools/build.sh (idempotent; safe to re-run and
# to cache). This is the setup documented in Mantella-AE104's
# research/notes.md section 3, automated verbatim -- same xwin splat flags,
# same vcpkg clone, same llvm-mingw release, same CLNG pin.
#
#   tools/setup-toolchain.sh
#
# Produces:
#   /tmp/xwin-winsysroot            MSVC CRT + Windows SDK (regenerable scratch)
#   ~/.local/share/vcpkg           vcpkg clone (ports tree; the binary comes
#                                  from nixpkgs inside tools/build.sh)
#   ~/.local/share/llvm-mingw       20260826 (builds the fxc2 stand-in for the
#                                  overlay directxtk port)
#   /tmp/clng                      CommonLibSSE-NG @ v6.8.0 (cloned by build.sh
#                                  too; cloned here so the setup is complete)
#
# Overridable env: XWIN_SYSROOT= XWIN_CACHE= VCPKG_ROOT= LLVM_MINGW=
#                  LLVM_MINGW_URL= CLNG=
set -euo pipefail

if [ -z "${FACEGENGUARD_SETUP_IN_NIX:-}" ]; then
    exec nix shell nixpkgs#git nixpkgs#curl nixpkgs#xz nixpkgs#stdenv.cc \
        -c env FACEGENGUARD_SETUP_IN_NIX=1 "$0" "$@"
fi

XWIN_SYSROOT=${XWIN_SYSROOT:-/tmp/xwin-winsysroot}
XWIN_CACHE=${XWIN_CACHE:-/tmp/xwin-cache}
VCPKG_ROOT=${VCPKG_ROOT:-$HOME/.local/share/vcpkg}
LLVM_MINGW=${LLVM_MINGW:-$HOME/.local/share/llvm-mingw}
CLNG=${CLNG:-/tmp/clng}

LLVM_MINGW_TAG=20260826
LLVM_MINGW_URL=${LLVM_MINGW_URL:-https://github.com/mstorsjo/llvm-mingw/releases/download/$LLVM_MINGW_TAG/llvm-mingw-$LLVM_MINGW_TAG-ucrt-ubuntu-22.04-x86_64.tar.xz}
CLNG_PIN=44dd911486bc43b05b55a23781c7e471eef86542  # v6.8.0, has Address Library V5

echo "== vcpkg clone at $VCPKG_ROOT"
if [ -d "$VCPKG_ROOT/ports" ]; then
    echo "   already present"
else
    mkdir -p "$(dirname "$VCPKG_ROOT")"
    git clone https://github.com/microsoft/vcpkg "$VCPKG_ROOT"
fi

echo "== llvm-mingw $LLVM_MINGW_TAG at $LLVM_MINGW"
if [ -x "$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++" ]; then
    echo "   already present"
else
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    curl -fsSL "$LLVM_MINGW_URL" -o "$tmp/llvm-mingw.tar.xz"
    mkdir -p "$LLVM_MINGW"
    tar -xJf "$tmp/llvm-mingw.tar.xz" -C "$tmp"
    # The archive unpacks to a versioned directory; normalize to $LLVM_MINGW.
    src=$(find "$tmp" -maxdepth 1 -type d -name 'llvm-mingw-*' | head -1)
    cp -a "$src"/. "$LLVM_MINGW"/
    "$LLVM_MINGW/bin/x86_64-w64-mingw32-clang++" --version | head -1
fi

echo "== xwin sysroot at $XWIN_SYSROOT (MSVC CRT + Windows SDK)"
if [ -d "$XWIN_SYSROOT/Windows Kits/10/Include" ]; then
    echo "   already present"
else
    nix run nixpkgs#xwin -- --accept-license --cache-dir "$XWIN_CACHE" \
        splat --output "$XWIN_SYSROOT" --include-debug-libs \
        --use-winsysroot-style --preserve-ms-arch-notation
fi

echo "== CommonLibSSE-NG at $CLNG (pinned $CLNG_PIN)"
if [ ! -d "$CLNG/.git" ]; then
    git clone https://github.com/alandtse/CommonLibVR "$CLNG"
fi
git -C "$CLNG" checkout -q "$CLNG_PIN"
git -C "$CLNG" log --oneline -1

echo "== toolchain ready"
