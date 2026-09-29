#!/usr/bin/env bash
# SUPERSEDED / DELEGATED: this script no longer assembles anything itself.
# Packaging is spec-driven now: fomod-package.toml + aviallon-mods/modforge's
# tools/package_fomod.py (spec schema in that file's docstring). This wrapper
# only stages the freshly built DLL where the spec expects it and delegates:
#
#   tools/package-fomod.sh [built-FaceGenGuard.dll] [output-dir]
#
# Defaults: DLL = ${WORK:-/tmp/facegenguard-build}/out/FaceGenGuard.dll,
#           output-dir = current directory (the zip name is *.zip, gitignored).
# The gate is tools/verify-fomod.py (also a thin delegate) -- run it before
# shipping, exactly like CI does. CI itself calls the modforge tools directly
# through the shared workflow (aviallon-mods/modforge/.github/workflows/
# build-fomod.yml@main) and never runs this script.
set -euo pipefail

REPO=$(cd "$(dirname "$0")/.." && pwd)
DLL=${1:-${WORK:-/tmp/facegenguard-build}/out/FaceGenGuard.dll}
OUTDIR=${2:-.}

[ -s "$DLL" ] || { echo "FATAL: no built DLL at '$DLL' (run tools/build.sh first)"; exit 1; }

# The spec's build input is the repo-relative build/out/FaceGenGuard.dll (what
# tools/build.sh writes when run with WORK=$PWD/build, as CI does).
STAGED="$REPO/build/out/FaceGenGuard.dll"
if [ "$(realpath "$DLL")" != "$(realpath -m "$STAGED")" ]; then
    mkdir -p "$REPO/build/out"
    cp "$DLL" "$STAGED"
fi
echo "== staged $(sha256sum "$STAGED" | cut -c1-16)… -> build/out/FaceGenGuard.dll"

MODFORGE=$("$REPO/tools/find-modforge.sh")
exec python3 "$MODFORGE/tools/package_fomod.py" \
    --spec "$REPO/fomod-package.toml" --out "$OUTDIR"
