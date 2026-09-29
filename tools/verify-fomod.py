#!/usr/bin/env python3
"""SUPERSEDED / DELEGATED: there is no FaceGenGuard-specific verification any
more. The shipping gate lives once, in aviallon-mods/modforge's
tools/verify_fomod.py (spec-driven, mutation-tested: exact entry list, >= 1
<plugin> in the ModuleConfig, payload byte-identity against the spec's
sources, stamped meta.ini). This wrapper only resolves that tool and the repo's
fomod-package.toml and delegates:

    tools/verify-fomod.py <FaceGenGuard-<version>-fomod.zip> [more.zip ...]

(The old `<zip> <built-dll>` CLI is gone: payload byte-identity is checked
against the spec's sources, so no separate DLL argument is needed.)

Every check prints exactly one narrow observed claim. Exit status: 0 all checks
pass, 1 any fails, 2 usage error -- same contract as before.
"""

import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    root = subprocess.run([str(REPO / "tools" / "find-modforge.sh")],
                          capture_output=True, text=True, check=True).stdout.strip()
    return subprocess.call([sys.executable, os.path.join(root, "tools", "verify_fomod.py"),
                            "--spec", str(REPO / "fomod-package.toml"), *sys.argv[1:]])


if __name__ == "__main__":
    sys.exit(main())
