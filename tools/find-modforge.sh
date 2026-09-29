#!/usr/bin/env bash
# Locate the aviallon-mods/modforge checkout that carries the shared FOMOD
# tools (package_fomod.py / verify_fomod.py) and print its root. The tools live
# ONLY there -- this repo's package-fomod.sh / verify-fomod.py are thin
# delegates so no second, contradictory packaging or verification
# implementation can survive here.
#
# Resolution order:
#   $MODFORGE                                   explicit override
#   <repo>/_modforge                            what CI checks out
#   ~/Programing/Personnal/modforge             the dev machine's checkout
#   ~/.cache/modforge                           cached clone (created on demand)
set -euo pipefail

if [ -n "${MODFORGE:-}" ] && [ -f "$MODFORGE/tools/package_fomod.py" ]; then
    echo "$MODFORGE"
    exit 0
fi

REPO=$(cd "$(dirname "$0")/.." && pwd)
for d in "$REPO/_modforge" "$HOME/Programing/Personnal/modforge"; do
    if [ -f "$d/tools/package_fomod.py" ]; then
        echo "$d"
        exit 0
    fi
done

CACHE="$HOME/.cache/modforge"
if [ ! -f "$CACHE/tools/package_fomod.py" ]; then
    git clone --depth 1 https://github.com/aviallon-mods/modforge "$CACHE"
fi
echo "$CACHE"
