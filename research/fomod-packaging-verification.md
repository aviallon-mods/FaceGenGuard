# FOMOD packaging verification — mutation-test evidence

Date: 2026-09-28. Branch: `feat/resilience-guard`.
Scope: `tools/verify-fomod.py` (the CI gate on `FaceGenGuard-<version>-fomod.zip`).

## What the gate must catch

The hand-built package whose `fomod/ModuleConfig.xml` had an empty
`<optionalFileGroups/>` crashed Amethyst's FOMOD wizard:
`selected_plugin or first_plugin` then `plugin.description`, with zero plugins
`first_plugin` is `None` → `AttributeError: 'NoneType' object has no attribute
'description'`. The XML was **well-formed**, so "it parses" is not a check. A
package whose XML yields zero `<plugin>` is INVALID and must fail CI.

## Claims under test (tools/verify-fomod.py)

1. `zip-entries` — the zip contains exactly
   `fomod/ModuleConfig.xml`, `SKSE/Plugins/FaceGenGuard.dll`,
   `SKSE/Plugins/FaceGenGuard.ini` (3 entries, no directory entries, no parent
   directory, no absolute/`..`/backslash paths).
2. `moduleconfig-parse` — `fomod/ModuleConfig.xml` parses as XML.
3. `moduleconfig-plugins` — `<optionalFileGroups>` yields **>= 1** `<plugin>`.
4. `moduleconfig-patterns` — at least one `<pattern>` copies `SKSE`.
5. `ini-resilience` — `SKSE/Plugins/FaceGenGuard.ini` has `[Resilience]` with a
   `bEnabled` key.
6. `dll-sha256` — the DLL in the zip has the same sha256 as the built
   `FaceGenGuard.dll` the zip was packaged from.

Inputs: `fomod/ModuleConfig.xml` (sha256 `d0a6a382f29b30472444fd064be9ee03f2cd510c8d38507ca1d6327a264dcb1c`),
`config/FaceGenGuard.ini` (sha256 `15c38e5861a3efb065cec35e300299afebbef8c2acf53d7b692b1a16765ac644`),
both byte-identical to the known-good reference package
(`~/Téléchargements/FaceGenGuard-0.1.0/pkg/…`); built DLL sha256
`a2dfe0f687f16facac50439bdddc7beca379fac5392b566e01a550c88804daba` (757760 B,
`tools/build.sh` output at `/tmp/facegenguard-build/out/FaceGenGuard.dll`).

## Method

For every run: mutate a **versioned source in the repo**, re-run the real
`tools/package-fomod.sh`, run `python3 tools/verify-fomod.py <zip> <built dll>`,
record the exit code and the pass/fail counts, restore with `git checkout --`
and assert the cleanup (`git status --porcelain` empty **and** `cmp` against the
reference bytes).

## Results — pass/fail counts per run

| Run | Zip under test | passed | failed | exit | verdict |
|---|---|---|---|---|---|
| 0 (baseline, good zip) | `FaceGenGuard-0.1.0-fomod.zip` | **6** | **0** | 0 | VERIFIED |
| 1 (`<optionalFileGroups/>`) | mutant 1 | **5** | **1** | 1 | INVALID |
| 2 (ini without `bEnabled`) | mutant 2 | **5** | **1** | 1 | INVALID |
| 3 (post-restore re-run) | good zip again | **6** | **0** | 0 | VERIFIED |

Baseline good-zip sha256 (locally packaged from the built DLL):
`ef676c4d6cb3c6bb7db45360470f7752b968c9b887acf810f1cf15a8f6861f65`.

### Run 1 — the exact wizard-killing defect

Mutation: `fomod/ModuleConfig.xml`, regex
`<optionalFileGroups>.*?</optionalFileGroups>` (one occurrence) →
`<optionalFileGroups/>`; repackaged with `tools/package-fomod.sh`.

Observed: the mutant **still passes** `moduleconfig-parse` ("parses as XML;
root element `<config>`", 592 B) and every other check except one. The failing
claim, verbatim:

```
FAIL moduleconfig-plugins: fomod/ModuleConfig.xml: <optionalFileGroups> yields 0 <plugin> (need >= 1) - Amethyst's wizard resolves `selected_plugin or first_plugin` then reads plugin.description, so 0 plugins dies with AttributeError: 'NoneType' object has no attribute 'description'
```

Result line: `RESULT: 5 passed, 1 failed, 6 checks - INVALID`, exit 1.

### Run 2 — ini without the master switch

Mutation: `config/FaceGenGuard.ini`, delete the line `bEnabled=1` (the
`[Resilience]` section itself kept); repackaged with `tools/package-fomod.sh`.

Observed failing claim, verbatim:

```
FAIL ini-resilience: SKSE/Plugins/FaceGenGuard.ini (48 B): [Resilience] has no bEnabled key (observed keys: ['uMaxSuppressions', 'sRvaWhitelist']) - the master switch would fall back to the compiled default instead of the shipped one
```

Result line: `RESULT: 5 passed, 1 failed, 6 checks - INVALID`, exit 1.

### Cleanup asserted (not assumed)

After each run: `git checkout -- <mutated file>`, then `git status --porcelain`
observed **empty** and `cmp` against the reference file observed **identical**
for both `fomod/ModuleConfig.xml` and `config/FaceGenGuard.ini`. Run 3 rebuilt
the zip from the restored tree and re-observed `6 passed, 0 failed, exit 0`.

## Narrow claims

- On 2026-09-28, in `FaceGenGuard @ feat/resilience-guard`, `tools/verify-fomod.py`
  on the good zip of 3 entries observed 6/6 checks PASS, exit 0.
- The same validator, on a zip whose `fomod/ModuleConfig.xml` had
  `<optionalFileGroups/>` (592 B, well-formed XML), observed 5 PASS / 1 FAIL,
  exit 1, the FAIL being `moduleconfig-plugins` with the AttributeError message
  above.
- The same validator, on a zip whose ini lacked `bEnabled` (48 B), observed
  5 PASS / 1 FAIL, exit 1, the FAIL being `ini-resilience`.
- Mutation artifacts (regenerable, disposable): `/tmp/fg-mutation/` —
  `good.zip`, `mutant1-empty-optionalFileGroups.zip`, `mutant2-ini-no-benabled.zip`
  and the per-run logs. The durable record is this file.
