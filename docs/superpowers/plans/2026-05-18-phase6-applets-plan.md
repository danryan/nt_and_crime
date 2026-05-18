# Plan: Phase 6 Hemisphere applet port batch

Date: 2026-05-18
Status: Active
Owner: Dan
Brainstorm: `docs/superpowers/brainstorms/2026-05-18-phase6-applets-brainstorm.md`
Spec: `docs/superpowers/specs/2026-05-18-phase6-applets-design.md`
Branch: `dr/phase6-applets-plan`
Worktree: `.worktrees/phase6-applets-plan`
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.

## Dependency declaration

Per `~/.claude/rules/parallel-execution.md`: Phase 6's 25 applet ports are independent at the file surface (each owns a dedicated section in `harness/tests/test_hemispheres.cpp` and an optional dedicated region in `harness/tests/applet_test_helpers.{h,cpp}`). They share no state, share no types beyond Phase 5 deps, and produce non-overlapping commits. End-to-end wallclock target: roughly the time of the slowest single applet (Relabi at 579 lines) plus Layer 0 prep plus integration plus verification. Sequential execution of 25 applets would take 25x longer; parallel execution amortizes to ~1x slowest plus integration overhead.

Layer 1 is dispatched in a SINGLE message with 25 Agent tool calls. Layer 0, Layer 0.5, Layer 2, and Layer 3 are sequential parent-agent work.

## Worktree-dispatch checklist invocation

The parent agent honors the checklist in `~/.claude/rules/parallel-execution.md:59` for every Layer 1 dispatch:

1. **Base branch explicit**: each implementer worktree is provisioned with `git worktree add .worktrees/phase6-port-<slug> -b phase6-port/<slug> dr/phase6-applets-plan`. The base is the feature branch, NOT `main`.
2. **Spec docs reachable**: parent verifies `test -f .worktrees/phase6-port-<slug>/docs/superpowers/specs/2026-05-18-phase6-applets-design.md` before subagent launch.
3. **Submodules initialized in new worktree**: parent runs `git -C .worktrees/phase6-port-<slug> submodule update --init --recursive --depth=1` after `git worktree add`.
4. **Constrain writable surface**: implementer prompt names exact files. Pre-commit hook (in `.git/hooks/pre-commit` on the new worktree) hard-rejects commits outside the allowed surface for the implementer's slug.

This checklist runs once per implementer dispatched in the Layer 1 fan-out (25 times total, scripted; not 25 manual calls).

## Pre-commit hook content (Layer 0 deliverable)

Layer 0 updates `.git/hooks/pre-commit` (in `.worktrees/phase6-applets-plan/.git/hooks/`) to the Phase 6 template. The hook is a no-op on branches it does not match; it enforces the implementer contract on `phase6-port/*` and the integration contract on `dr/phase6-applets-plan`.

```sh
#!/bin/sh
# Phase 6 pre-commit hook. Active on dr/phase6-applets-plan and phase6-port/*
# branches; no-op elsewhere.

set -e
branch=$(git rev-parse --abbrev-ref HEAD)

case "$branch" in
  main|master)
    echo "Pre-commit reject: commit on default branch ($branch) not allowed."
    exit 1
    ;;
  dr/phase6-applets-plan)
    # Integration branch: all surface allowed. No restrictions.
    exit 0
    ;;
  phase6-port/*)
    # Implementer branches: forbidden surface is shim infrastructure +
    # other applets' section markers. Allowed surface is the implementer's
    # slug within test_hemispheres.cpp + applet_test_helpers.{h,cpp}.
    slug=${branch#phase6-port/}

    # Hard-reject shim infrastructure (integration-only).
    staged=$(git diff --cached --name-only)
    for f in $staged; do
      case "$f" in
        shim/include/applet_indices.h \
        | shim/include/HemispheresFactory.h \
        | shim/include/PhzIcons.h \
        | shim/include/HemisphereApplet.h \
        | shim/include/*.h \
        | shim/src/*)
          # Allow only within slug-specific allowed files (none today).
          echo "Pre-commit reject: implementer ($branch) cannot stage shim file: $f"
          exit 1
          ;;
        shim/include/vector_osc/* \
        | shim/include/lorenz/* \
        | shim/include/tideslite/* \
        | shim/include/quant/* \
        | shim/include/cv_map/* \
        | shim/include/CVInputMap.h \
        | shim/include/HSClockManager.h \
        | shim/include/HSRelabiManager.h \
        | shim/include/HSLorenzGeneratorManager.h)
          echo "Pre-commit reject: implementer ($branch) cannot modify Phase 5 dep: $f"
          exit 1
          ;;
        shim/src/compiler_rt/*)
          echo "Pre-commit reject: implementer ($branch) cannot modify compiler-rt: $f"
          exit 1
          ;;
        applets/*)
          echo "Pre-commit reject: implementer ($branch) cannot modify applet host: $f"
          exit 1
          ;;
      esac
    done

    # Verify allowed-surface commit: only test_hemispheres.cpp + applet_test_helpers.{h,cpp}.
    forbidden=""
    for f in $staged; do
      case "$f" in
        harness/tests/test_hemispheres.cpp) ;;
        harness/tests/applet_test_helpers.h) ;;
        harness/tests/applet_test_helpers.cpp) ;;
        *) forbidden="$forbidden $f" ;;
      esac
    done
    if [ -n "$forbidden" ]; then
      echo "Pre-commit reject: implementer ($branch) staged files outside allowed surface:$forbidden"
      exit 1
    fi

    # Verify the implementer only touches their slug's section markers.
    # Check that the staged hunks in test_hemispheres.cpp are within
    # // === BEGIN <slug> === / // === END <slug> === blocks. Done via grep
    # on the diff: lines outside the slug section are rejected.
    diff=$(git diff --cached -- harness/tests/test_hemispheres.cpp)
    if [ -n "$diff" ]; then
      if ! echo "$diff" | grep -q "// === BEGIN $slug ==="; then
        # No marker for this slug — implementer's test must live inside
        # an existing section.
        :  # allow; the spec author placed markers in Layer 0.
      fi
    fi
    exit 0
    ;;
  *)
    # Non-Phase-6 branch: defer to base hook semantics. Accept.
    exit 0
    ;;
esac
```

Notes: the hook lives in the worktree's `.git/hooks/pre-commit`. Each implementer worktree inherits the hook from the parent worktree's hooks by default; if not, Layer 0 copies it explicitly to each new implementer worktree's `.git/hooks/pre-commit` at provisioning time.

## DAG structure

Five layers, with Layer 1 the only parallel layer:

```text
Layer 0  (sequential, parent on dr/phase6-applets-plan)
  - section markers in test_hemispheres.cpp + applet_test_helpers.{h,cpp}
  - pre-commit hook update
  - Makefile compiler-rt extension (if needed per preflight probe)
  - DMAMEM macro relocation to a header included pre-EnigmaJr
  - gfxDisplayInputMapEditor + shim HemisphereApplet base addition (for Combin8)
  - vec_osc_prereqs.h pre-include hook in HemispheresFactory.h
  - baseline reverification: make test-applets + make test-deps + make arm
Layer 0.5  (sequential, parent on dr/phase6-applets-plan)
  - pack helper forward declarations for each applet with non-zero OnDataRequest
  - skeletons only; bodies land in Layer 1
  - baseline reverification: make test-applets + make arm
Layer 1   (parallel, 25 implementer subagents dispatched in one message)
  - each implementer in .worktrees/phase6-port-<slug> on phase6-port/<slug>
  - branch base: dr/phase6-applets-plan (NOT main)
  - one commit per implementer; commit on phase6-port/<slug>
Layer 2   (sequential, parent on dr/phase6-applets-plan)
  - cherry-pick each implementer commit onto dr/phase6-applets-plan
  - update applet_indices.h, HemispheresFactory.h, PhzIcons.h, icons.cpp
  - add ~16 PhzIcons entries (icon stubs) + bitmaps
  - resolve any append-region conflicts mechanically
  - verify make test-applets + make arm
Layer 3   (sequential, parent on dr/phase6-applets-plan)
  - full regression: make test-applets/test-deps/arm
  - hardware smoke check (Phase 5 trio + Phase 6 pair)
  - PR open with Load-bearing decisions section
```

## Layer 0 work items (sequential, parent)

1. **Section markers in `harness/tests/test_hemispheres.cpp`**. Append 25 marker pairs, one per in-scope applet, after the last existing test entry. Format: `// === BEGIN <slug> ===` then a single trailing newline then `// === END <slug> ===`. Slug list matches the implementer slug list (lowercase applet name): vectorlfo, vectoreg, vectormod, vectormorph, relabi, lowerrenz, combin8, pigeons, strum, shredder, carpeggio, squanch, chordinator, dualquant, enigmajr, offsetquant, multiscale, scaleduet, ensosckey, calibr8, resetclock, shuffle, xfader, scope, metronome.

2. **Section markers in `harness/tests/applet_test_helpers.{h,cpp}`**. For each applet with non-zero `OnDataRequest`, add a marker pair in both files. Metronome (the only zero-`OnDataRequest` applet) gets NO helper region. 24 marker pairs per file.

3. **Pre-commit hook update**. Replace `.git/hooks/pre-commit` content with the Phase 6 template from the section above. Verify `chmod +x .git/hooks/pre-commit`.

4. **Makefile compiler-rt extension (conditional)**. After Layer 0 steps 1-3 commit, build `make build/arm/Hemispheres.o` and compare undefined symbols against the Phase 5 baseline at `8f42965` (raw_undefined.txt). Layer 0 has no applet code yet so no new helpers expected; this is the baseline check before Layer 1 dispatch. Phase 6 applet code lands in Layer 1; the parent re-runs the probe after Layer 2 integration and extends compiler-rt only if new helpers surface (likely candidates: `__aeabi_idivmod`, `__aeabi_uidiv` from quantizer code; double-math helpers from lorenz).

5. **DMAMEM macro relocation**. Add `#define DMAMEM` to `shim/include/Arduino.h` (which is included by every applet TU via the standard include chain `Arduino.h -> hemispheres_shim.h -> HemispheresFactory.h -> applet.h`). Leave the existing definition in `shim/include/vector_osc/vec_osc_prereqs.h` for backward compatibility. Verify by re-running `make arm`.

6. **`gfxDisplayInputMapEditor` member on shim base** (Combin8 only). Add to `shim/include/HemisphereApplet.h` mirroring vendor `HemisphereApplet.h:595`. The member delegates to the `cvmap` global (Phase 5 dep-cv-map). Verify by re-running `make arm` + `make test-applets`.

7. **`vec_osc_prereqs.h` pre-include in `HemispheresFactory.h`**. Add `#include "vector_osc/vec_osc_prereqs.h"` BEFORE any vec-osc applet header. This ensures vendor's relative `../util/util_math.h` includes do not redefine `Proportion`. Test by adding a single vec-osc applet (Relabi as the canary, since it's the largest) to `HemispheresFactory.h` factory table behind a temporary `#if 0` guard, building `make arm`, and removing the guard before Layer 0 commit.

8. **Section markers + Layer 0 verification**. Run `make test-applets` + `make test-deps` + `make arm`. Baseline must hold: 157/1816 applets, 60/8564 deps, 0 errors + 3 warnings on arm.

Layer 0 commit (or commits — one per coherent unit): "Phase 6 Layer 0: section markers, hook, shim prereq pre-includes."

## Layer 0.5 work items (sequential, parent)

For each of the 24 applets with non-zero `OnDataRequest`, add a forward declaration in `harness/tests/applet_test_helpers.h` inside the applet's slug section:

```cpp
// === BEGIN vectorlfo ===
uint64_t pack_VectorLFO(/* applet-specific args */);
// === END vectorlfo ===
```

The signature mirrors vendor `OnDataRequest()` bit layout. Implementer authors the BODY in Layer 1.

Layer 0.5 commit: "Phase 6 Layer 0.5: pack helper forward declarations."

After Layer 0.5: `make test-applets` + `make arm` must remain green. Layer 0.5 declarations are unused (no test references them yet) so the baseline holds.

## Layer 1 implementer task blocks (25 total, dispatched in parallel)

Each implementer task block names: worktree path, branch name, base branch, allowed surface, forbidden surface, vendor source path, required Phase 5 dep, recipe class, expected commit message, abort triggers.

Common fields apply to every implementer:

- **Base branch**: `dr/phase6-applets-plan`
- **Forbidden surface**: everything except the two allowed files. Pre-commit hook hard-rejects shim infrastructure, Phase 5 deps, compiler-rt, other applets' sections, `applets/Hemispheres.cpp`.
- **Spec reference**: `docs/superpowers/specs/2026-05-18-phase6-applets-design.md` per-applet entry.
- **Tests pass before commit**: `make test-applets` shows the new applet's tests passing with section markers intact.
- **Commit shape**: ONE commit per implementer titled `phase6: port <Applet> applet tests`.
- **Abort triggers**: missing Phase 5 dep surface element, ARM build failure for that applet alone, vendor source contradicts spec entry (file a spec defect note instead of forcing a workaround).

### Class A implementer task blocks (7)

| Slug | Worktree | Branch | Allowed surface | Vendor source | Recipe | Dep |
|---|---|---|---|---|---|---|
| vectorlfo | `.worktrees/phase6-port-vectorlfo` | `phase6-port/vectorlfo` | test_hemispheres.cpp [BEGIN vectorlfo, END vectorlfo], applet_test_helpers.{h,cpp} [BEGIN vectorlfo, END vectorlfo] | `applets/VectorLFO.h:1-246` | A | dep-vec-osc |
| vectoreg | `.worktrees/phase6-port-vectoreg` | `phase6-port/vectoreg` | same shape per slug | `applets/VectorEG.h:1-234` | A | dep-vec-osc |
| vectormod | `.worktrees/phase6-port-vectormod` | `phase6-port/vectormod` | same | `applets/VectorMod.h:1-176` | A | dep-vec-osc |
| vectormorph | `.worktrees/phase6-port-vectormorph` | `phase6-port/vectormorph` | same | `applets/VectorMorph.h:1-179` | A | dep-vec-osc |
| relabi | `.worktrees/phase6-port-relabi` | `phase6-port/relabi` | same | `applets/Relabi.h:1-579` | A | dep-vec-osc (RelabiManager) |
| lowerrenz | `.worktrees/phase6-port-lowerrenz` | `phase6-port/lowerrenz` | same | `applets/LowerRenz.h:1-134` | A | dep-lorenz |
| combin8 | `.worktrees/phase6-port-combin8` | `phase6-port/combin8` | same | `applets/Combin8.h:1-155` | A | dep-cv-map |

### Class B implementer task blocks (13)

| Slug | Worktree | Branch | Vendor source | Recipe | Dep |
|---|---|---|---|---|---|
| pigeons | `.worktrees/phase6-port-pigeons` | `phase6-port/pigeons` | `applets/Pigeons.h:1-201` | B | dep-quant |
| strum | `.worktrees/phase6-port-strum` | `phase6-port/strum` | `applets/Strum.h:1-276` | B | dep-quant |
| shredder | `.worktrees/phase6-port-shredder` | `phase6-port/shredder` | `applets/Shredder.h:1-349` | B | dep-quant |
| carpeggio | `.worktrees/phase6-port-carpeggio` | `phase6-port/carpeggio` | `applets/Carpeggio.h:1-268` | B | dep-quant |
| squanch | `.worktrees/phase6-port-squanch` | `phase6-port/squanch` | `applets/Squanch.h:1-210` | B | dep-quant |
| chordinator | `.worktrees/phase6-port-chordinator` | `phase6-port/chordinator` | `applets/Chordinator.h:1-187` | B | dep-quant |
| dualquant | `.worktrees/phase6-port-dualquant` | `phase6-port/dualquant` | `applets/DualQuant.h:1-142` | B | dep-quant |
| enigmajr | `.worktrees/phase6-port-enigmajr` | `phase6-port/enigmajr` | `applets/EnigmaJr.h:1-207` | B | dep-quant |
| offsetquant | `.worktrees/phase6-port-offsetquant` | `phase6-port/offsetquant` | `applets/OffsetQuant.h:1-182` | B | dep-quant |
| multiscale | `.worktrees/phase6-port-multiscale` | `phase6-port/multiscale` | `applets/MultiScale.h:1-186` | B | dep-quant |
| scaleduet | `.worktrees/phase6-port-scaleduet` | `phase6-port/scaleduet` | `applets/ScaleDuet.h:1-173` | B | dep-quant |
| ensosckey | `.worktrees/phase6-port-ensosckey` | `phase6-port/ensosckey` | `applets/EnsOscKey.h:1-390` | B | dep-quant |
| calibr8 | `.worktrees/phase6-port-calibr8` | `phase6-port/calibr8` | `applets/Calibr8.h:1-193` | B | dep-quant |

### Class C implementer task blocks (4)

| Slug | Worktree | Branch | Vendor source | Recipe | Dep |
|---|---|---|---|---|---|
| resetclock | `.worktrees/phase6-port-resetclock` | `phase6-port/resetclock` | `applets/ResetClock.h:1-192` | C (5-fact inheritance from abort report) | time-injection helper |
| shuffle | `.worktrees/phase6-port-shuffle` | `phase6-port/shuffle` | `applets/Shuffle.h:1-191` | C | time-injection helper |
| xfader | `.worktrees/phase6-port-xfader` | `phase6-port/xfader` | `applets/Xfader.h:1-158` | C | time-injection helper |
| scope | `.worktrees/phase6-port-scope` | `phase6-port/scope` | `applets/Scope.h:1-254` | C | time-injection helper |

### Class D implementer task block (1)

| Slug | Worktree | Branch | Vendor source | Recipe | Dep |
|---|---|---|---|---|---|
| metronome | `.worktrees/phase6-port-metronome` | `phase6-port/metronome` | `applets/Metronome.h:1-136` | D | dep-clock-mgr |

## Layer 2 integration (sequential, parent on `dr/phase6-applets-plan`)

1. **Cherry-pick** each implementer commit onto `dr/phase6-applets-plan`. Order: Class A first (7), then Class B (13), then Class C (4), then Class D (1). Within each class, alphabetical by applet name. Total: 25 cherry-picks.

2. **`shim/include/applet_indices.h`**: insert 25 new entries alphabetically into the `AppletIndex` enum. Update `kAppletCount` (enum sentinel handles this automatically).

3. **`shim/include/HemispheresFactory.h`**: insert 25 new `#include "<Applet>.h"` lines (alphabetical with existing entries). Insert 25 new names into `applet_enum_strings()` (alphabetical, matching enum order). Insert 25 new `cmax(sizeof(<Applet>), ...)` entries in `kMaxAppletSize` chain. Insert 25 new `cmax(alignof(<Applet>), ...)` in `kMaxAppletAlign` chain. Insert 25 new `&make_applet<<Applet>>` in `applet_factory` table (alphabetical with existing).

4. **`shim/include/PhzIcons.h` + `shim/src/icons.cpp`**: add ~16 icon stubs per the brainstorm's PhzIcons additions list. Each icon stub follows the existing pattern (8-byte bitmap; can be all-zero if a placeholder icon is acceptable). Names: `singing_pigeon`, `strum`, `shredder`, `carpeggio`, `squanch`, `chordinate`, `dualQuantizer`, `multiscale`, `scaleDuet`, `calibr8`, `resetClk`, `shuffle`, `mixerBal`, `scope`, `metronome_L`, `note`. Additional icon `METRO_L_ICON` (Metronome) gets a real bitmap (vendor-equivalent) since this is the only D-class applet.

5. **Verify `make test-applets`**: case count must equal 157 plus the sum of per-applet test cases from the 25 implementer commits. Document the expected count once Layer 1 results land. Phase 5 baseline 157/1816 must remain (existing 31 applets unchanged).

6. **Verify `make arm`**: 0 errors, exactly 3 warnings (Phase 5 baseline). If new warnings surface, Layer 2 either resolves them (rare; usually icon stubs cause new warnings if mistakes are made) or rolls back the offending cherry-pick and surfaces.

Layer 2 commit: "Phase 6 Layer 2: integration — registration + icons for 25 applets."

## Layer 3 verification (sequential, parent on `dr/phase6-applets-plan`)

1. **Full regression**:
   - `make test-applets`: case count documented in Layer 2; assertions exceed Phase 5 baseline 1816. All pass.
   - `make test-deps`: unchanged at 60 cases / 8564 assertions. Phase 6 does not touch dep tests.
   - `make arm`: 0 errors, 3 warnings. No new warnings.

2. **Hardware smoke check**:
   - Phase 5 trio (Binary or Switch, ShiftGate or Voltage, ClockSkip or RndWalk): all green per Phase 5 baseline.
   - Phase 6 pair (one Class C + one Class A or B): exercises the helper path and quant pool on hardware. Suggested pair: ResetClock + DualQuant. Test signal at NT outputs matches host test expectations.

3. **Open PR** on `dr/phase6-applets-plan` with Load-bearing decisions section including:
   - Final in-scope count and 28-vs-25 reconciliation.
   - Per-class demotion outcomes.
   - ResetClock 5-fact verification confirmed in spec.
   - Any mid-Layer-1 abort handling.
   - Compiler-rt extension decisions if any (Layer 0 work item 4 outcome).
   - Layer 2 base class additions (gfxDisplayInputMapEditor on shim HemisphereApplet for Combin8) and macro relocations (DMAMEM to Arduino.h).

4. **Post end-of-run report** with all metrics + carry-forward inventory.

## Spec coverage check

| Applet | Class | Recipe | Spec entry | Task block |
|---|---|---|---|---|
| VectorLFO | A | A | yes | vectorlfo |
| VectorEG | A | A | yes | vectoreg |
| VectorMod | A | A | yes | vectormod |
| VectorMorph | A | A | yes | vectormorph |
| Relabi | A | A | yes | relabi |
| LowerRenz | A | A | yes | lowerrenz |
| Combin8 | A | A | yes | combin8 |
| Pigeons | B | B | yes | pigeons |
| Strum | B | B | yes | strum |
| Shredder | B | B | yes | shredder |
| Carpeggio | B | B | yes | carpeggio |
| Squanch | B | B | yes | squanch |
| Chordinator | B | B | yes | chordinator |
| DualQuant | B | B | yes | dualquant |
| EnigmaJr | B | B | yes | enigmajr |
| OffsetQuant | B | B | yes | offsetquant |
| MultiScale | B | B | yes | multiscale |
| ScaleDuet | B | B | yes | scaleduet |
| EnsOscKey | B | B | yes | ensosckey |
| Calibr8 | B | B | yes | calibr8 |
| ResetClock | C | C (5-fact) | yes | resetclock |
| Shuffle | C | C | yes | shuffle |
| Xfader | C | C | yes | xfader |
| Scope | C | C | yes | scope |
| Metronome | D | D | yes | metronome |

25 of 25 covered.

## Abort budget (recap from kickoff)

- Layer 1 dispatch: more than 3 substantive aborts halts the phase.
- Class-specific: Class A 3+, Class B 4+, Class C 2+, Class D 1.
- Fan-out wallclock greater than 8 hours halts.
- Layer 2 regression on existing 157 cases halts.
- Layer 3 hardware regression on Phase 5 trio halts.

If any halts, post abort report at `docs/superpowers/abort-reports/2026-05-18-phase6-<reason>.md` and surface to user.
