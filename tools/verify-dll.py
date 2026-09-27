#!/usr/bin/env python3
"""Verify a CommonLibSSE-NG SKSE plugin DLL without running it.

Checks (all evidence-based, no "it compiled so it works"):
  * the three SKSE exports exist (SKSEPlugin_Load/Query/Version);
  * SKSEPlugin_Version (an exported *variable* holding SKSE::PluginDeclaration)
    parses: name, author, struct compatibility, runtime compatibility
    (byte 0 bit 0 = AddressLibrary version independence);
  * the V5 address-library reader strings are present (runtime 1.7.104 ships
    V5 bins; a plugin built against pre-V5 CommonLibSSE-NG cannot read them).

Usage: tools/verify-dll.py <plugin.dll> [...]
"""
import struct
import sys


def parse_pe(data: bytes):
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[e_lfanew:e_lfanew + 4] == b"PE\0\0", "not a PE file"
    coff = e_lfanew + 4
    nsections, = struct.unpack_from("<H", data, coff + 2)
    opt_size, = struct.unpack_from("<H", data, coff + 16)
    opt = coff + 20
    magic, = struct.unpack_from("<H", data, opt)
    assert magic == 0x20B, f"not PE32+ (magic {magic:#x})"
    image_base, = struct.unpack_from("<Q", data, opt + 24)
    sections = []
    sect = opt + opt_size
    for i in range(nsections):
        off = sect + i * 40
        name = data[off:off + 8].rstrip(b"\0").decode()
        vsize, va, rawsize, raw = struct.unpack_from("<IIII", data, off + 8)
        sections.append((name, va, vsize, raw, rawsize))
    return image_base, sections


def rva_to_off(sections, rva):
    for _, va, vsize, raw, rawsize in sections:
        if va <= rva < va + max(vsize, rawsize):
            return raw + (rva - va)
    raise ValueError(f"RVA {rva:#x} not in any section")


def exports(data, sections):
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    opt = e_lfanew + 24
    exp_rva, exp_size = struct.unpack_from("<II", data, opt + 112)
    if not exp_rva:
        return {}
    exp = rva_to_off(sections, exp_rva)
    nnames, = struct.unpack_from("<I", data, exp + 0x18)
    names_rva, = struct.unpack_from("<I", data, exp + 0x20)
    ords_rva, = struct.unpack_from("<I", data, exp + 0x24)
    funcs_rva, = struct.unpack_from("<I", data, exp + 0x1C)
    out = {}
    for i in range(nnames):
        name_rva, = struct.unpack_from("<I", data, rva_to_off(sections, names_rva) + 4 * i)
        noff = rva_to_off(sections, name_rva)
        name = data[noff:data.index(b"\0", noff)].decode()
        ord_, = struct.unpack_from("<H", data, rva_to_off(sections, ords_rva) + 2 * i)
        fn_rva, = struct.unpack_from("<I", data, rva_to_off(sections, funcs_rva) + 4 * ord_)
        out[name] = fn_rva
    return out


def cstr(buf, off, maxlen):
    end = buf.index(b"\0", off, off + maxlen) if b"\0" in buf[off:off + maxlen] else off + maxlen
    return buf[off:end].decode(errors="replace")


def verify(path):
    data = open(path, "rb").read()
    image_base, sections = parse_pe(data)
    exp = exports(data, sections)
    ok = True

    missing = [n for n in ("SKSEPlugin_Load", "SKSEPlugin_Query", "SKSEPlugin_Version") if n not in exp]
    print(f"== {path}")
    print(f"   exports: {sorted(exp)}")
    if missing:
        print(f"   FAIL: missing exports {missing}")
        ok = False

    if "SKSEPlugin_Version" in exp:
        off = rva_to_off(sections, exp["SKSEPlugin_Version"])
        decl = data[off:off + 0x400]
        # SKSE::PluginDeclaration layout (authoritative static_asserts in
        # SKSE/Interfaces.h + the _dataVersion u32 preamble of PluginDeclaration):
        # +0x000 _dataVersion(u32, expect 1), +0x004 Version(u32 packed,
        # major<<24|minor<<16|patch<<4|build), +0x008 Name[256],
        # +0x108 Author[256], +0x208 SupportEmail[252], +0x304
        # StructCompatibility(u32), +0x308 RuntimeCompatibility (u32,
        # byte0 bit0 = AddressLibrary), +0x34C MinimumSKSEVersion(u32 packed)
        data_version, = struct.unpack_from("<I", decl, 0x000)
        packed, = struct.unpack_from("<I", decl, 0x004)
        name = cstr(decl, 0x08, 256)
        author = cstr(decl, 0x108, 256)
        email = cstr(decl, 0x208, 252)
        struct_compat, = struct.unpack_from("<I", decl, 0x304)
        runtime_compat, = struct.unpack_from("<I", decl, 0x308)
        version = ((packed >> 24) & 0xFF, (packed >> 16) & 0xFF,
                   (packed >> 4) & 0xFFF, packed & 0xF)
        addr_lib = runtime_compat & 1
        print(f"   dataVersion={data_version} name={name!r} author={author!r} "
              f"version={version[0]}.{version[1]}.{version[2]}.{version[3]} "
              f"struct_compatibility={struct_compat} (1=Independent) "
              f"runtime_compatibility={runtime_compat:#x} (addressLibrary={addr_lib})")
        if data_version != 1:
            print("   FAIL: unexpected _dataVersion -- struct layout not recognized")
            ok = False
        if addr_lib != 1:
            print("   FAIL: runtime compatibility is not AddressLibrary")
            ok = False

    text = data
    v5_markers = (b"offset count is invalid", b"memory-map the address library")
    have = [m.decode() for m in v5_markers if m in text]
    print(f"   V5 reader markers: {have}")
    if not have:
        print("   FAIL: no Address Library V5 reader strings -- this build cannot "
              "read the 1.7.104 versionlib bins")
        ok = False

    print(f"   {'OK' if ok else 'FAILED'}")
    return ok


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sys.exit(0 if all(verify(p) for p in sys.argv[1:]) else 1)