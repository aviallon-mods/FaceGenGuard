#!/usr/bin/env bash
# Assemble the installable FOMOD zip: FaceGenGuard-<version>-fomod.zip.
#
# Inputs (all versioned or freshly built -- nothing is generated ad hoc):
#   - the freshly built FaceGenGuard.dll   (tools/build.sh output)
#   - fomod/ModuleConfig.xml               (versioned, byte-identical reference)
#   - config/FaceGenGuard.ini              (versioned, byte-identical reference)
#
# Layout inside the zip -- exactly three file entries, no directory entries, no
# parent directory:
#   fomod/ModuleConfig.xml
#   SKSE/Plugins/FaceGenGuard.dll
#   SKSE/Plugins/FaceGenGuard.ini
#
# The zip is only ASSEMBLED here; tools/verify-fomod.py is the gate that decides
# whether the result is a package Amethyst can actually install. CI runs the two
# together, and so should you.
#
#   tools/package-fomod.sh [built-FaceGenGuard.dll] [output-dir]
#
# Defaults: DLL = ${WORK:-/tmp/facegenguard-build}/out/FaceGenGuard.dll,
#           output-dir = current directory (the zip name is *.zip, gitignored).
# The version comes from CMakeLists.txt (project(... VERSION x.y.z)), the same
# single source of truth that stamps FG_VERSION into the DLL: a version hardcoded
# here once produced an asset whose name lied about its contents.
set -euo pipefail

# Tools come from an expression, never a raw /nix/store path and never a system
# install: if zip or python3 is missing, re-exec inside `nix shell` (same
# pattern as tools/build.sh).
if [ -z "${FACEGENGUARD_PACKAGE_IN_NIX:-}" ]; then
    if ! command -v zip >/dev/null || ! command -v python3 >/dev/null; then
        exec nix shell nixpkgs#zip nixpkgs#python3 \
            -c env FACEGENGUARD_PACKAGE_IN_NIX=1 "$0" "$@"
    fi
fi

REPO=$(cd "$(dirname "$0")/.." && pwd)
DLL=${1:-${WORK:-/tmp/facegenguard-build}/out/FaceGenGuard.dll}
OUTDIR=${2:-.}

VERSION=$(sed -nE 's/^project\(FaceGenGuard VERSION ([0-9][0-9.]*).*/\1/p' "$REPO/CMakeLists.txt")
[ -n "$VERSION" ] || { echo "FATAL: no project(... VERSION x.y.z) in CMakeLists.txt"; exit 1; }

[ -s "$DLL" ] || { echo "FATAL: no built DLL at '$DLL' (run tools/build.sh first)"; exit 1; }
[ -s "$REPO/fomod/ModuleConfig.xml" ] || { echo "FATAL: missing fomod/ModuleConfig.xml"; exit 1; }
[ -s "$REPO/config/FaceGenGuard.ini" ] || { echo "FATAL: missing config/FaceGenGuard.ini"; exit 1; }

mkdir -p "$OUTDIR"
OUTDIR=$(cd "$OUTDIR" && pwd)
ZIP="$OUTDIR/FaceGenGuard-$VERSION-fomod.zip"

# Disposable staging tree (a build product; regenerable from the inputs above).
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/fomod" "$STAGE/SKSE/Plugins"
cp "$REPO/fomod/ModuleConfig.xml" "$STAGE/fomod/ModuleConfig.xml"
cp "$DLL" "$STAGE/SKSE/Plugins/FaceGenGuard.dll"
cp "$REPO/config/FaceGenGuard.ini" "$STAGE/SKSE/Plugins/FaceGenGuard.ini"

rm -f "$ZIP"
# Explicit file list (not a directory walk) so the zip gets exactly three file
# entries: no fomod/ SKSE/ SKSE/Plugins/ directory entries, no parent dir.
# -X strips the platform extra fields (uid/gid/timestamps) from the entries.
( cd "$STAGE" && zip -X -q "$ZIP" \
    fomod/ModuleConfig.xml \
    SKSE/Plugins/FaceGenGuard.dll \
    SKSE/Plugins/FaceGenGuard.ini )

echo "== packaged: $ZIP"
sha256sum "$ZIP"
python3 - "$ZIP" <<'EOF'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    for i in z.infolist():
        print(f"  {i.file_size:>8}  {i.filename}")
EOF
echo "$ZIP"
