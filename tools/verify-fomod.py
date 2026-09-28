#!/usr/bin/env python3
"""Verify a packaged FaceGenGuard FOMOD zip before it can be shipped.

A zip can be well-formed and still be INVALID. The defect this gate exists for:
`fomod/ModuleConfig.xml` whose `<optionalFileGroups>` yields ZERO `<plugin>`
elements (e.g. an empty `<optionalFileGroups/>`). Amethyst's FOMOD wizard
resolves `selected_plugin or first_plugin` and then reads `plugin.description`;
with zero plugins `first_plugin` is None and the wizard dies with
`AttributeError: 'NoneType' object has no attribute 'description'`. Such a zip
is rejected HERE, not on the user's machine.

Usage:
    tools/verify-fomod.py <FaceGenGuard-<version>-fomod.zip> <built FaceGenGuard.dll>

Every check prints exactly one narrow observed claim (what was read, where, with
which numbers). Exit status 0 only when ALL checks pass, 1 when any fails (2 on
usage error). The checks are independent: one failing check does not hide the
verdict on the others.
"""

import hashlib
import re
import sys
import xml.etree.ElementTree as ET
import zipfile

# The entire allowed contents of the package. Anything else -- a directory
# entry, a parent directory, a stray README -- is a failed check.
EXPECTED_ENTRIES = [
    "fomod/ModuleConfig.xml",
    "SKSE/Plugins/FaceGenGuard.dll",
    "SKSE/Plugins/FaceGenGuard.ini",
]


def localname(tag):
    """XML local name, namespace-agnostic."""
    return tag.rsplit("}", 1)[-1]


def descendants(el, name):
    return [e for e in el.iter() if localname(e.tag) == name]


class Ctx:
    def __init__(self, zip_path, dll_path):
        self.zip_path = zip_path
        self.dll_path = dll_path
        with zipfile.ZipFile(zip_path) as z:
            self.names = z.namelist()
            self.entries = {}
            for n in self.names:
                if not n.endswith("/"):
                    self.entries[n] = z.read(n)
        self.xml_root = None  # populated by check_xml_parse


def check_zip_entries(ctx):
    observed = sorted(ctx.names)
    ok = observed == sorted(EXPECTED_ENTRIES) and len(ctx.names) == len(EXPECTED_ENTRIES)
    # Also refuse hostile path shapes even if the count happened to match.
    hostile = [n for n in ctx.names
               if n.startswith("/") or "\\" in n or ".." in n.split("/")]
    ok = ok and not hostile
    if ok:
        return True, (f"{ctx.zip_path}: exactly 3 file entries {observed}; "
                      f"no directory entries, no parent directory, all paths relative")
    return False, (f"{ctx.zip_path}: expected exactly the 3 entries {sorted(EXPECTED_ENTRIES)}, "
                   f"observed {len(ctx.names)} entries {observed}"
                   + (f"; hostile path shapes {hostile}" if hostile else ""))


def check_xml_parse(ctx):
    name = "fomod/ModuleConfig.xml"
    data = ctx.entries.get(name)
    if data is None:
        return False, (f"{name}: absent from {ctx.zip_path} "
                       f"(observed entries: {sorted(ctx.names)})")
    try:
        root = ET.fromstring(data)
    except ET.ParseError as e:
        return False, f"{name} ({len(data)} B): does not parse as XML: {e}"
    ctx.xml_root = root
    return True, (f"{name} ({len(data)} B): parses as XML; root element "
                  f"<{localname(root.tag)}>")


def check_moduleconfig_plugins(ctx):
    """THE defect guard: <optionalFileGroups> must yield >= 1 <plugin>."""
    name = "fomod/ModuleConfig.xml"
    if ctx.xml_root is None:
        return False, f"{name}: cannot count <plugin> - the XML did not parse (see the check above)"
    groups = descendants(ctx.xml_root, "optionalFileGroups")
    plugins = [p for g in groups for p in descendants(g, "plugin")]
    plugin_names = [p.get("name") for p in plugins]
    if plugins:
        return True, (f"{name}: <optionalFileGroups> yields {len(plugins)} <plugin> "
                      f"{plugin_names} (need >= 1)")
    return False, (f"{name}: <optionalFileGroups> yields 0 <plugin> (need >= 1) - "
                   f"Amethyst's wizard resolves `selected_plugin or first_plugin` then reads "
                   f"plugin.description, so 0 plugins dies with "
                   f"AttributeError: 'NoneType' object has no attribute 'description'")


def check_moduleconfig_patterns(ctx):
    name = "fomod/ModuleConfig.xml"
    if ctx.xml_root is None:
        return False, f"{name}: cannot count <pattern> - the XML did not parse (see the check above)"
    patterns = descendants(ctx.xml_root, "pattern")
    observed = []
    copying_skse = 0
    for p in patterns:
        sources = [s.get("name") for s in descendants(p, "source")]
        observed.append((p.get("name"), sources))
        if any(s == "SKSE" or (s or "").startswith("SKSE/") for s in sources):
            copying_skse += 1
    if copying_skse:
        return True, (f"{name}: {copying_skse} of {len(patterns)} <pattern> copies SKSE "
                      f"(observed patterns: {observed})")
    return False, (f"{name}: 0 of {len(patterns)} <pattern> copies SKSE "
                   f"(observed patterns: {observed}) - the install would copy nothing")


def parse_ini(text):
    """Return {section_lower: [(key, value, line_no), ...]} with observed line numbers."""
    sections = {}
    current = None
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith(";") or line.startswith("#"):
            continue
        m = re.fullmatch(r"\[(.+)\]", line)
        if m:
            current = m.group(1).strip().lower()
            sections.setdefault(current, [])
            continue
        if "=" in line and current is not None:
            key, value = line.split("=", 1)
            sections[current].append((key.strip(), value.strip(), lineno))
    return sections


def check_ini_resilience(ctx):
    name = "SKSE/Plugins/FaceGenGuard.ini"
    data = ctx.entries.get(name)
    if data is None:
        return False, (f"{name}: absent from {ctx.zip_path} "
                       f"(observed entries: {sorted(ctx.names)})")
    text = data.decode("utf-8-sig", errors="replace")
    sections = parse_ini(text)
    if "resilience" not in sections:
        return False, (f"{name} ({len(data)} B): no [Resilience] section "
                       f"(observed sections: {sorted(sections)})")
    keys = sections["resilience"]
    hit = [(k, v, n) for k, v, n in keys if k.lower() == "benabled"]
    if not hit:
        return False, (f"{name} ({len(data)} B): [Resilience] has no bEnabled key "
                       f"(observed keys: {[k for k, _, _ in keys]}) - the master switch would fall "
                       f"back to the compiled default instead of the shipped one")
    k, v, n = hit[0]
    return True, (f"{name} ({len(data)} B): [Resilience] defines {k}={v} at line {n}; "
                  f"observed keys {[k for k, _, _ in keys]}")


def check_dll_sha256(ctx):
    name = "SKSE/Plugins/FaceGenGuard.dll"
    data = ctx.entries.get(name)
    if data is None:
        return False, (f"{name}: absent from {ctx.zip_path} "
                       f"(observed entries: {sorted(ctx.names)})")
    try:
        with open(ctx.dll_path, "rb") as f:
            built = f.read()
    except OSError as e:
        return False, f"built DLL {ctx.dll_path}: cannot be read: {e}"
    zipped_sha = hashlib.sha256(data).hexdigest()
    built_sha = hashlib.sha256(built).hexdigest()
    if zipped_sha == built_sha:
        return True, (f"{name} sha256={zipped_sha} ({len(data)} B) == built {ctx.dll_path} "
                      f"sha256={built_sha} ({len(built)} B)")
    return False, (f"{name} sha256={zipped_sha} ({len(data)} B) != built {ctx.dll_path} "
                   f"sha256={built_sha} ({len(built)} B) - the zip does not contain the DLL it "
                   f"was supposedly packaged from")


CHECKS = [
    ("zip-entries", check_zip_entries),
    ("moduleconfig-parse", check_xml_parse),
    ("moduleconfig-plugins", check_moduleconfig_plugins),
    ("moduleconfig-patterns", check_moduleconfig_patterns),
    ("ini-resilience", check_ini_resilience),
    ("dll-sha256", check_dll_sha256),
]


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    ctx = Ctx(argv[1], argv[2])
    passed = failed = 0
    for check_id, fn in CHECKS:
        try:
            ok, claim = fn(ctx)
        except Exception as e:  # a crashed check is a failed check, never a silent pass
            ok, claim = False, f"check raised {type(e).__name__}: {e}"
        print(f"{'PASS' if ok else 'FAIL'} {check_id}: {claim}")
        passed, failed = (passed + 1, failed) if ok else (passed, failed + 1)
    verdict = "VERIFIED" if failed == 0 else "INVALID"
    print(f"RESULT: {passed} passed, {failed} failed, {len(CHECKS)} checks - {verdict}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
