# FaceGenGuard

A small, **self-contained SKSE plugin that survives one known Skyrim crash
instead of dying**. It is explicitly **behaviour-changing** and **off by
default**: when enabled, a memory fault inside one whitelisted engine function
is recovered from instead of crashing the game.

It exists because of a real crash (Skyrim AE **1.7.104.0**, RaceMenu face-gen
path): a near-NULL object dereference mid-function in the engine function
identified by **Address Library ID 26938** (`mov r15d,[rbp+0x40]` with
`rbp = 0x10`, 0x86 bytes into the function). The crash is real and must not be
hidden — this plugin's bet is that for *this one* fault, abandoning the call is
better than losing the session.

## What it does

On `EXCEPTION_ACCESS_VIOLATION` **whose RIP falls inside the whitelisted code
range — and ONLY there** — the guard:

1. unwinds the faulting function to its **caller's** context
   (`RtlVirtualUnwind` with `UNW_FLAG_NHANDLER`, using the function's `.pdata`
   unwind info),
2. sets `RAX = 0` in that caller context,
3. continues execution there (`EXCEPTION_CONTINUE_EXECUTION`).

Every other exception, every other RIP, a failed unwind, or an exhausted
suppression budget is passed on untouched (`EXCEPTION_CONTINUE_SEARCH`) — the
whitelist is the **entire scope** of the feature.

The whitelist is computed at **runtime** from the Address Library:
`[entry, entry + 0x800)` where `entry` is `REL::Relocation{ REL::ID(26938) }`
for the running game build. No RVA is hardcoded at runtime. Before installing,
the guard verifies the function's prologue bytes (the ones verified for
1.7.104.0) and **refuses to run** on a build it does not recognise or on a
function that was patched before it — catching faults in the wrong function
would be worse than crashing.

### Honest limitations — read these

- **The unwind abandons the function's half-done side effects.** Whatever the
  faulting call already wrote to the world stays written; only its remaining
  work is skipped. The game continues in a state the engine never planned.
- **`RAX = 0` is only a safe return for scalar, pointer and bool returns.** A
  struct-by-value return (hidden `sret` pointer in `RCX`) is NOT covered.
- **Fail-open cap.** After `uMaxSuppressions` suppressions (default 64) the
  guard stops suppressing and lets the crash happen, so one known crash cannot
  become an unknown, repeatedly-corrupted state that keeps running. The total
  and the cap are reported in the normal log every 60 s.
- **It fixes nothing.** The underlying bug (a mod or the engine dereferencing a
  dead/absent object) is still there. This is a survival tool for a session,
  not a repair.

## Logging — allocation-free handler path

The handler runs on the faulting thread of an already-faulting process, so the
handler path allocates **nothing**: no `std::string`, no spdlog, no `snprintf`,
no heap. The log file handle is **pre-opened at load** and each suppression
writes one fixed-format line through `WriteFile`:

```
resilience-guard: suppress #1 rip=0x7FF612AB34CD rva=0x12AB34CD fault=0x50 badPtr=0x10 (rbp+0x40) callerRip=0x7FF612AB9010
```

`badPtr` is the bad pointer taken from the faulting instruction's base register
(the register whose value plus the instruction's displacement gives the fault
address — a documented heuristic, no instruction decoding). If the log file
cannot be opened at load, **nothing is suppressed**: a suppression that cannot
be recorded is not an honest suppression.

Suppression totals are reported in the normal (non-handler) log path:

```
resilience-guard: 3/64 suppressions used, 0 refused by the fail-open cap
```

## Configuration — `Data/SKSE/Plugins/FaceGenGuard.ini`

| Key | Default | Meaning |
|---|---|---|
| `[Resilience] bEnabled` | `0` | Master switch. `0` installs nothing at all. **Behaviour-changing** when on. |
| `[Resilience] uMaxSuppressions` | `64` | Fail-open cap: after this many suppressions, let the crash happen. |
| `[Resilience] sRvaWhitelist` | *(empty)* | Optional whitelist override. Empty = the built-in Address Library target (`ID 26938`, `[entry, entry+0x800)`). Non-empty = exactly these hex RVA ranges instead (`0xAAAAAA-0xBBBBBB, ...`, end exclusive, relative to `SkyrimSE.exe`), for a build where the built-in target does not apply. |

The `Default` column is the compiled fallback used when the file (or the key)
is absent. The FOMOD zip ships `config/FaceGenGuard.ini` verbatim as
`SKSE/Plugins/FaceGenGuard.ini`, with `bEnabled=1`: installing the package is
what turns the guard on.

When the guard is enabled the startup banner says so, unmissably:

```
config summary: resilience=1 cap=64 behaviourChanging=resilience-guard (faulting call unwound to its caller, rax=0)
```

when it is off:

```
config summary: resilience=0 cap=64 behaviourChanging=none (semantics-preserving)
```

## Build

The plugin is a CommonLibSSE-NG (v6.8.0) SKSE plugin for Skyrim AE 1.7.104
(Address Library V5). `tools/build.sh` cross-builds it to a Windows x64 DLL
(MSVC ABI) **on Linux** — clang-cl + lld-link + xwin sysroot + vcpkg
(`x64-windows-clangcl` triplet, the Mantella-AE104 toolchain, mirrored):

```
tools/setup-toolchain.sh   # one-time: xwin sysroot, vcpkg, llvm-mingw, CLNG
tools/build.sh             # -> /tmp/facegenguard-build/out/FaceGenGuard.dll
```

Dependencies are pinned: CommonLibSSE-NG `alandtse/CommonLibVR @ 44dd911`
(v6.8.0 — older CLNG cannot read the 1.7.104 `versionlib` bins) through the
overlay port in `build-overlay/ports/commonlibsse-ng`, and a recent
microsoft/vcpkg baseline (`vcpkg-configuration.json`).

## Tests (off-game)

`tests/` builds the handler's decision core (`src/Core/ResiliencePolicy.cpp` —
the whitelist matcher, the suppression counter and cap, the log-line formatter)
as plain C++ with no Windows dependency and RUNS it on Linux **and** Windows:

```
cmake -S tests -B build-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests
./build-tests/fgtests
```

The suite covers the whitelist RIP matcher (inside, at both edges, outside, a
null whitelist), the suppression counter and the fail-open cap (the handler
must decline to suppress after the cap and must count exactly), and the log
line (exact fixed format, truncation safety, and **zero heap allocations** —
the harness replaces global `operator new`/`delete` with counting wrappers, and
the probe is itself tested against a deliberately non-elidable allocation).
CI runs the suite on both platforms, cross-builds the DLL, and packages +
verifies the installable FOMOD zip (a bad package fails CI, not the wizard).

## Layout

```
src/
  main.cpp                 SKSE entry point, banner, status thread
  Config.{h,cpp}           FaceGenGuard.ini ([Resilience])
  ResilienceGuard.{h,cpp}  Windows glue: Address Library target, prologue check,
                           vectored handler, RtlVirtualUnwind, WriteFile logging
  Core/
    ResiliencePolicy.{h,cpp} the handler's decision logic (plain C++, unit-tested)
tests/                     off-game suite + heap-allocation probe
tools/                     build.sh (Linux -> MSVC ABI), setup-toolchain.sh,
                           verify-dll.py (PE/export/Address-Library-V5 checks),
                           package-fomod.sh + verify-fomod.py (the FOMOD zip
                           and the CI gate that verifies its shape)
fomod/ModuleConfig.xml     FOMOD wizard definition (packaged verbatim)
build-overlay/             vcpkg overlay port + triplet (the proven toolchain)
config/FaceGenGuard.ini    the shipped ini, packaged verbatim into the FOMOD zip
```

## Licence

MIT. CommonLibSSE-NG is GPL-3.0-or-later with the Modding Exception (linked,
not modified).
