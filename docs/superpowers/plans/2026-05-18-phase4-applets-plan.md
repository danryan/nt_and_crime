# Plan: Phase 4 Hemisphere applet ports

Date: 2026-05-18
Status: Active
Owner: Dan
Brainstorm: `docs/superpowers/brainstorms/2026-05-18-phase4-applets-brainstorm.md`
Spec: `docs/superpowers/specs/2026-05-18-phase4-applets-design.md`
Branch: `dr/phase4-applets-plan`
Worktree: `.worktrees/phase4-applets-plan`

## Goal

Land tests for 7 Hemisphere applet ports on `dr/phase4-applets-plan`. Open a PR. ResetClock demoted to Phase 5.

## Dependency declaration

This plan executes under `~/.claude/rules/parallel-execution.md`. Inlined substance: the worktree-dispatch checklist requires the parent agent to (a) specify the base branch explicitly in `git worktree add`, (b) verify the spec docs are reachable inside the new worktree before subagent launch, (c) verify submodules are initialized inside the new worktree, (d) constrain the subagent's writable surface via pre-commit hook plus prompt language. The recovery procedure mandates that subagents abort on missing prereqs rather than synthesizing them.

Default is parallel. A task serializes only when (a) a later item structurally depends on an earlier item's chosen abstractions or types, (b) items modify the same logical region in incompatible ways, or (c) the task is small enough that orchestration overhead exceeds the parallel gain.

In this plan: Layer 0 shim infrastructure tasks sequence within themselves (one commit at a time, with `make test-applets` and `make arm` between each) to constrain blast radius. Layer 0.5 section markers depend on Layer 0 completion. Layer 1 implementer tasks parallelize once Layer 0 and Layer 0.5 commit. Layer 2 integration sequences after Layer 1. Layer 3 verification sequences after Layer 2.

End-to-end wallclock target: Layer 0 (~1 hour) + Layer 1 slowest implementer (~30 minutes) + Layer 2 integration (~20 minutes) + Layer 3 verification (~20 minutes) = roughly 2-2.5 hours.

## Worktree-dispatch checklist (parent agent's responsibility)

Before dispatching any implementer subagent (Layer 1), the parent agent runs these steps per implementer worktree:

1. Specify the base branch explicitly: `git worktree add <path> -b phase4-port/<applet-slug> dr/phase4-applets-plan`. The base MUST be `dr/phase4-applets-plan` AFTER Layer 0 and Layer 0.5 have committed, not before. Never rely on the worktree tool's default base.
2. Verify the spec doc is reachable inside the new worktree before the subagent launches: `test -f <worktree>/docs/superpowers/specs/2026-05-18-phase4-applets-design.md` and `test -f <worktree>/docs/superpowers/plans/2026-05-18-phase4-applets-plan.md`. If either is missing, the dispatch is broken; abort it.
3. Verify submodules are initialized inside the new worktree: `git -C <worktree> submodule update --init --depth=1 vendor/O_C-Phazerville vendor/distingNT_API`. Worktrees do not inherit submodule state.
4. Install the pre-commit hook (next section) at `<worktree>/.git/hooks/pre-commit` (or the linked-worktree hooks path) and `chmod +x` it.
5. State the implementer's allowed and forbidden file surface explicitly in the dispatch prompt. The hook enforces the forbidden surface; the prose names it.

## Pre-commit hook

The parent agent installs this hook in each implementer worktree at the worktree's `.git/worktrees/<applet>/hooks/pre-commit` and `chmod +x` it. The hook lives outside the working tree (in the worktree's git common dir), so it does not show up in `git diff` and the implementer cannot accidentally commit a modified hook.

```sh
#!/bin/sh
# Phase 4 implementer pre-commit hook.
# Rejects commits that stage forbidden-surface changes or run from a
# worktree branched from the wrong base.

set -e

# Forbidden surface for Phase 4 implementers.
# Phase 4-shim/* branches relax this for shim/ paths only.
forbidden='shim/include/applet_indices.h shim/include/HemispheresFactory.h shim/include/PhzIcons.h shim/include/HemisphereApplet.h shim/include/HSUtils.h shim/include/HSicons.h shim/include/hemispheres_shim.h shim/include/Arduino.h shim/include/OC_core.h shim/include/HSIOFrame.h shim/include/SegmentDisplay.h shim/include/HSProbLoopLinker.h shim/src/icons.cpp shim/src/globals.cpp shim/src/graphics.cpp applets/Hemispheres.cpp'

branch=$(git rev-parse --abbrev-ref HEAD)
staged=$(git diff --cached --name-only)

# Phase 4 commits accepted on:
#   - dr/phase4-applets-plan (Layer 0, Layer 0.5, Layer 2 integration)
#   - phase4-port/<slug>     (Layer 1 implementer, forbidden surface enforced)
#   - phase4-shim/<slug>     (optional Layer 0 isolated shim, forbidden surface relaxed)
case "$branch" in
  dr/phase4-applets-plan)
    : # accept all
    ;;
  phase4-port/*)
    for f in $forbidden; do
        if echo "$staged" | grep -qx "$f"; then
            echo "pre-commit: refusing to commit forbidden-surface file: $f" >&2
            echo "pre-commit: integration task owns this file; remove from the commit." >&2
            exit 1
        fi
    done
    ;;
  phase4-shim/*)
    : # shim work allowed
    ;;
  *)
    echo "pre-commit: refusing to commit on branch '$branch' that does not match Phase 4 accept-list." >&2
    echo "pre-commit: re-create the worktree with: git worktree add <path> -b phase4-port/<slug> dr/phase4-applets-plan" >&2
    exit 1
    ;;
esac

# Verify the worktree's branch derives from dr/phase4-applets-plan, not main.
if ! git merge-base --is-ancestor dr/phase4-applets-plan HEAD 2>/dev/null; then
    echo "pre-commit: refusing to commit on branch '$branch' that is not derived from dr/phase4-applets-plan." >&2
    echo "pre-commit: re-create the worktree with: git worktree add <path> -b <branch> dr/phase4-applets-plan" >&2
    exit 1
fi

exit 0
```

The hook is installed by the parent agent during worktree creation. The implementer never edits this hook. If the hook rejects a commit, the implementer un-stages the forbidden file and continues; the integration task (Layer 2) picks up shim infra edits.

## File structure

### Layer 1 implementer surface

Each implementer task produces edits to exactly these files in the implementer's worktree:

- `harness/tests/applet_test_helpers.h`: one `uint64_t pack_<applet>(...)` decl between the applet's section markers. Skip for Binary (empty `OnDataRequest`).
- `harness/tests/applet_test_helpers.cpp`: one `pack_<applet>` impl between the applet's section markers. Skip for Binary.
- `harness/tests/test_hemispheres.cpp`: `using hem_shim::kApplet<Name>;`, optional `<applet>_set(hi, ...)` helper, N `TEST_CASE`s between the applet's section markers.

### Forbidden in implementer commits (enforced by hook)

All files listed in the pre-commit hook's `forbidden` variable.

### Owned by Layer 0 / Layer 0.5 / Layer 2 on `dr/phase4-applets-plan`

- Layer 0: `shim/include/HSicons.h`, `shim/include/HemisphereApplet.h`, `shim/include/Arduino.h`, `shim/include/hemispheres_shim.h`, `shim/include/SegmentDisplay.h` (new), `shim/include/HSProbLoopLinker.h` (new), `shim/include/PhzIcons.h`, `shim/src/icons.cpp`.
- Layer 0.5: `harness/tests/test_hemispheres.cpp`, `harness/tests/applet_test_helpers.h`, `harness/tests/applet_test_helpers.cpp` (section markers only; no logic).
- Layer 2: `shim/include/applet_indices.h`, `shim/include/HemispheresFactory.h`, `shim/include/PhzIcons.h` (registration entries only), plus any mechanical conflict resolution between section markers across the cherry-picked implementer commits.

The implementer cannot validate test syntax through a full build because `applet_indices.h` lacks the new enum entries until Layer 2. The implementer compiles the test file in isolation against the section markers via `make test-applets`, which works once Layer 0 and Layer 0.5 land (the section markers contain no logic but the registration enums exist only for already-ported applets).

Wait: `make test-applets` requires `kAppletBinary`, `kAppletShiftGate`, etc. to exist for `using hem_shim::kApplet<Name>;` to compile. The implementer cannot fully compile in their worktree until Layer 2 lands those enums.

**Resolution**: implementer task verification is a name-resolution check via the implementer's local `make test-applets` ONLY for the applets already in `applet_indices.h` plus their own (which Layer 1 cannot add - Layer 2 owns the enum). The implementer instead validates by:

1. `make test-applets` runs after the implementer adds their test bodies between section markers. The build will fail with an undeclared `kAppletXxx` error for the new applet. The implementer treats this single class of failure as expected.
2. The implementer's test bodies are reviewed by inspection (test names, assertion shapes) against the spec entry, not by build success.
3. Layer 2 integration verifies the full build (including the new enums).

This is a known trade-off of the parallel-implementer pattern. Reviewers verify that each implementer's commit (after cherry-pick by Layer 2) passes `make test-applets`. Layer 2 is the gate.

## Implementer task shape

Each implementer task is one paragraph stating: vendor file, category, link to the spec entry, named analogue, expected commit message. The implementer reads the spec entry and produces the pack helper plus test cases per the recipe. No paste-verbatim test bodies appear in this plan; the pattern lives in the spec recipe.

If the implementer discovers a spec defect mid-task (vendor `Controller()` contradicts the spec entry's observables claim, missing shim helper not anticipated by Layer 0, `OnDataReceive` constraint not declared in the spec), the implementer aborts with `BLOCKED` status and reports the defect. The parent agent revises the spec entry before re-dispatch.

## Layer 0: shim infrastructure (sequenced on `dr/phase4-applets-plan`)

Each Layer 0 commit lands on `dr/phase4-applets-plan` directly (not in an isolated worktree, per `using-git-worktrees` Step 0 -- the parent worktree itself is the isolated workspace). Between each commit, `make test-applets` and `make arm` must pass before the next is started. Layer 0 commits are logically independent but sequenced operationally to constrain blast radius. Approx 1 hour total.

1. **L0.1 LOOP_ICON + X_NOTE_ICON.** Add extern decls to `shim/include/HSicons.h` (after `RESET_ICON`). Add bitmap content to `shim/src/icons.cpp` copying verbatim from `vendor/HSicons.h:65` and `:91`. Unblocks: ShiftGate, ProbabilityDivider. Commit subject: `feat(shim): add LOOP_ICON + X_NOTE_ICON for Phase 4 applets`.
2. **L0.2 HemisphereApplet::Changed(int ch).** Add `bool Changed(int ch) { return HS::frame.changed_cv[ch + channel_offset()]; }` to `shim/include/HemisphereApplet.h`. In `shim/include/hemispheres_shim.h` `step()`, after `copy_bus_to_frame` for the 4 inputs and before the inner-tick loop, compute `changed_cv[i]` for each of the 4 channels as `abs(inputs[i] - last_cv[i]) > 32`, then update `last_cv[i] = inputs[i]`. `last_cv[4]` is declared static (file-scope) inside the shim. Unblocks: Trending. Commit subject: `feat(shim): add HemisphereApplet::Changed() with frame.changed_cv update`.
3. **L0.3 DrawSlider method.** Add `void DrawSlider(uint8_t x, uint8_t y, uint8_t len, uint8_t value, uint8_t max_val, bool is_cursor)` to `shim/include/HemisphereApplet.h` as an inline method drawing a slider via `gfxFrame` + `gfxRect` like the existing `gfxSkyline` pattern. Vendor signature at `vendor/HemisphereApplet.h:541-572`. View-only; no Out() impact. Unblocks: ProbabilityDivider. Commit subject: `feat(shim): add HemisphereApplet::DrawSlider for ProbabilityDivider View()`.
4. **L0.4 randomSeed + micros.** Add `inline void randomSeed(uint32_t seed) { extern uint32_t hem_rng_state; hem_rng_state = seed ? seed : 0xDEADBEEFu; }` to `shim/include/HemisphereApplet.h` near the existing `random()` macro. Add `inline uint32_t micros() { return (uint32_t)OC::CORE::ticks; }` to `shim/include/Arduino.h` (after the constrain helpers). Unblocks: ProbabilityDivider. Commit subject: `feat(shim): add randomSeed + micros Arduino-API shims`.
5. **L0.5 ProbLoopLinker stub.** Create `shim/include/HSProbLoopLinker.h` (new file mirroring vendor path). Stub class with: static singleton via `get()`, fields `uint16_t seed = 0`, `int loopStep = 0`, `bool isLooping = false`, `bool registered[2] = {false, false}`, no-op `RegisterDiv/UnloadDiv/RegisterMelo/UnloadMelo`, `IsLinked()` returning false, `SetLooping`, `TriggerRegeneration`, `SetLoopStep`, `Trigger(int ch)` (sets `trig_q[ch]`), `TrigPop(int ch)` returning false (no Melo linked), `GetSeed`/`SetSeed` maintaining the `seed` field. Include from `shim/include/HemisphereApplet.h` so applets see it. Unblocks: ProbabilityDivider. Commit subject: `feat(shim): add ProbLoopLinker stub singleton`.
6. **L0.6 SegmentDisplay stub.** Create `shim/include/SegmentDisplay.h` (new file). Stub class with `enum SegmentSize { BIG_SEGMENTS, TINY_SEGMENTS }`, constructor taking `SegmentSize` (no-op), `SetPosition(uint8_t x, uint8_t y)` (no-op), `PrintDigit(uint8_t)` (no-op). View-only. Unblocks: Binary. Commit subject: `feat(shim): add SegmentDisplay stub for Binary applet View()`.
7. **L0.7 Icon bitmaps for Phase 4 applets.** Add to `shim/include/PhzIcons.h`: `extern const uint8_t binaryCounter[8]; extern const uint8_t shiftGate[8]; extern const uint8_t trending[8]; extern const uint8_t probDiv[8]; extern const uint8_t AD_EG[8]; extern const uint8_t ADSR_EG[8]; extern const uint8_t gameOfLife[8];`. Add bitmap content to `shim/src/icons.cpp` copying verbatim from `vendor/PhzIcons.h:38,45,52,55,60,61,87`. Unblocks: all Phase 4 in-scope applets. Commit subject: `feat(shim): add PhzIcons bitmaps for Phase 4 applets`.

Layer 0 verification gate: between each of L0.1 through L0.7, run `make test-applets` and `make arm`. Both must pass before the next commit lands. Any failure halts Layer 0 (per abort budget).

## Layer 0.5: section markers (single commit on `dr/phase4-applets-plan`)

Single commit adding placeholder section markers in 3 files. Lands after Layer 0 completes; before any Layer 1 dispatch.

Files: `harness/tests/test_hemispheres.cpp`, `harness/tests/applet_test_helpers.h`, `harness/tests/applet_test_helpers.cpp`.

For each Phase 4 in-scope applet (Binary, ShiftGate, Trending, ProbabilityDivider, ADEG, ADSREG, GameOfLife), add a marker pair to each of the 3 files (skip the two helper files for Binary, which has no pack helper). Order alphabetically (matches Phase 3 retry's integration order). Within `test_hemispheres.cpp`, place markers at the file's tail (after the last existing TEST_CASE), separated by a blank line.

```cpp
// === BEGIN binary ===
// === END binary ===

// === BEGIN shift_gate ===
// === END shift_gate ===

// ... etc
```

Verification: `make test-applets` and `make arm` must pass with no regressions (the markers are comments, so impact is zero). Commit subject: `chore(test): add Phase 4 section markers in test files`.

## Layer 1: parallel applet implementer tasks

After Layer 0 and Layer 0.5 have committed and verified, dispatch all 7 implementer tasks in a single parallel batch (multiple `Agent` tool calls in one message). Each implementer runs in its own isolated worktree branched from the current head of `dr/phase4-applets-plan`. Each implementer's worktree has the pre-commit hook installed by the parent agent.

### Layer 1 task list

Each task is one paragraph. The implementer reads the spec entry and produces edits to the three implementer-owned files only, between its own section markers. The expected commit message is `test(applets): <applet> tests for <NN> branches`. The implementer reports back: worktree path, branch name, commit SHA, test output (the locally-failing build due to missing enum is expected), and any vendor-reality adjustments.

1. **Binary**. Vendor: `vendor/O_C-Phazerville/software/src/applets/Binary.h`. Category: A. Spec entry: "Binary" in spec. Analogue: GatedVCA / Button. No pack helper. Branch: `phase4-port/binary`. Expected commit subject: `test(applets): binary B1-B4 binary-counter + count outputs`.
2. **ShiftGate**. Vendor: `vendor/O_C-Phazerville/software/src/applets/ShiftGate.h`. Category: A. Spec entry: "ShiftGate". Analogue: RunglBook. Pack helper: `pack_shift_gate(length0, length1, trigger0, trigger1, reg0)`. Branch: `phase4-port/shift_gate`. Expected commit subject: `test(applets): shift_gate SG1-SG5 16-bit shift register + per-side trigger/gate`.
3. **Trending**. Vendor: `vendor/O_C-Phazerville/software/src/applets/Trending.h`. Category: A. Spec entry: "Trending". Analogue: EnvFollow. Pack helper: `pack_trending(assign0, assign1, sensitivity)`. Branch: `phase4-port/trending`. Expected commit subject: `test(applets): trending TR1-TR4 trend-bucket assignments + sensitivity`.
4. **ProbabilityDivider**. Vendor: `vendor/O_C-Phazerville/software/src/applets/ProbabilityDivider.h`. Category: A. Spec entry: "ProbabilityDivider". Analogue: Brancher + Cumulus. Pack helper: `pack_prob_div(weight_1, weight_2, weight_4, weight_8, loop_length, seed)`. Coverage shape: round-trip + state-injection ONLY (no bus-level fire-count assertion). Branch: `phase4-port/prob_div`. Expected commit subject: `test(applets): prob_div PD1-PD4 round-trip + state-injection (no fire-count)`.
5. **ADEG**. Vendor: `vendor/O_C-Phazerville/software/src/applets/ADEG.h`. Category: B. Spec entry: "ADEG". Analogue: Slew. Pack helper: `pack_adeg(attack, decay)`. Branch: `phase4-port/adeg`. Expected commit subject: `test(applets): adeg AE1-AE5 envelope integrator + EOC`.
6. **ADSREG**. Vendor: `vendor/O_C-Phazerville/software/src/applets/ADSREG.h`. Category: B. Spec entry: "ADSREG". Analogue: EnvFollow. Pack helper: `pack_adsreg(a0, d0, s0, r0, a1, d1, s1, r1)` (8 params, all 8-bit, no bias). Branch: `phase4-port/adsreg`. Expected commit subject: `test(applets): adsreg AR1-AR5 ADSR state machine + per-channel settings`.
7. **GameOfLife**. Vendor: `vendor/O_C-Phazerville/software/src/applets/GameOfLife.h`. Category: B (borderline). Spec entry: "GameOfLife". Analogue: RunglBook. Pack helper: `pack_game_of_life(weight)`. Branch: `phase4-port/game_of_life`. Expected commit subject: `test(applets): game_of_life GL1-GL4 board density + CV position`.

All 7 are independent; no shared state outside section markers. Dispatch them in a single message with 7 parallel `Agent` tool uses.

## Layer 2: integration

Sequenced after all Layer 1 commits land. Single integration commit on `dr/phase4-applets-plan`:

1. Cherry-pick each Layer 1 commit onto `dr/phase4-applets-plan`. Conflicts within a section marker pair resolve mechanically (concatenate additive content). Conflicts in `applet_test_helpers.h/cpp` resolve by section marker (each implementer touched only its own). No conflicts expected outside section markers because Layer 0.5 placed each implementer's edits in disjoint regions.
2. Add registration entries in alpha order to `shim/include/applet_indices.h` (7 new enum values: `kAppletADEG`, `kAppletADSREG`, `kAppletBinary`, `kAppletGameOfLife`, `kAppletProbabilityDivider`, `kAppletShiftGate`, `kAppletTrending`).
3. Add 7 corresponding entries to `shim/include/HemispheresFactory.h`: 7 new `#include`s, 7 new entries in `applet_enum_strings()`, 7 new entries in each of the two `cmax` chains, 7 new entries in `applet_factory()` table. All alpha-ordered.
4. Add 7 corresponding extern decls in `shim/include/PhzIcons.h` (already landed in Layer 0.7 but re-verify alpha ordering).
5. Brace-balance check: `make test-applets` builds the integrated tree; if compile fails on missing closing brace or content placed outside markers, run the Python repair script from Phase 3 retry (preserved at `harness/scripts/repair_braces.py`) as fallback. Repair must restore balance; if it cannot, surface as Layer 2 integration concern and halt.
6. Run `make test-applets`. Must pass with no regressions against the baseline of 125 cases and 1626 assertions (Phase 4 adds 25-40 test cases approximately, all under new tags).
7. Run `make arm`. Must succeed with zero warnings.

Commit subject: `feat: integrate Phase 4 (Binary, ShiftGate, Trending, ProbabilityDivider, ADEG, ADSREG, GameOfLife)`.

## Layer 3: final verification

Sequenced after Layer 2.

1. `make test-applets`: must pass with no regressions (baseline 125 cases, 1626 assertions; Phase 4 expected to add 25-40 cases).
2. `make arm`: must succeed with zero warnings.
3. Hardware smoke check on 3 applets covering distinct recipe variations:
   - **Binary** (cat-A, empty-OnDataRequest, Gate/CV-input combinational logic).
   - **Trending** (cat-A deferral, depends on new `Changed()` shim method).
   - **ADEG** (cat-B, envelope integrator with 10x rule, depends on new icon).
   The smoke check loads the Hemispheres plug-in on hardware, selects each applet for both halves, drives test inputs from a sequencer, and confirms the output behavior matches host-test expectations. Pass criterion: outputs visible on scope match what `read_cv_at` returns under equivalent input drive.

If any verification step fails, halt and report. Otherwise proceed to PR.

## Brace-eating mitigation

Section markers (Layer 0.5) are the primary defense against the Phase 3 retry integration's 9-missing-closing-braces / 6-missing-pack-helper-closing-braces hazard. Implementers add content only between markers; `make test-applets` (run before commit) surfaces structural defects.

The Python repair script (`harness/scripts/repair_braces.py`) from Phase 3 retry is retained as a Layer 2 fallback. If the script triggers (Layer 2 build fails on brace mismatch), Layer 2 surfaces this as an integration concern but does not halt unless the repaired tree still fails to compile.

## Spec coverage check

| Applet | Layer | Task | Spec entry |
| --- | --- | --- | --- |
| LOOP_ICON, X_NOTE_ICON | L0.1 | shim icon decls + bitmaps | Recipe / Shim prereq verification |
| Changed() + changed_cv update | L0.2 | shim method + frame update | Recipe / Shim prereq verification |
| DrawSlider | L0.3 | shim method | Recipe / Shim prereq verification |
| randomSeed + micros | L0.4 | shim Arduino API | Recipe / Shim prereq verification |
| ProbLoopLinker stub | L0.5 | shim singleton | Recipe / Shim prereq verification |
| SegmentDisplay stub | L0.6 | shim stub class | Recipe / Shim prereq verification |
| Phase 4 PhzIcons bitmaps | L0.7 | shim icon bitmaps | Recipe / Shim prereq verification |
| Section markers in test files | L0.5 (preparatory) | placeholder markers | Recipe / Section markers subsection |
| Binary | L1.1 | implementer test commit | Binary |
| ShiftGate | L1.2 | implementer test commit | ShiftGate |
| Trending | L1.3 | implementer test commit | Trending |
| ProbabilityDivider | L1.4 | implementer test commit | ProbabilityDivider |
| ADEG | L1.5 | implementer test commit | ADEG |
| ADSREG | L1.6 | implementer test commit | ADSREG |
| GameOfLife | L1.7 | implementer test commit | GameOfLife |
| Phase 4 enum + factory entries | L2 | integration commit | Recipe / Integration ordering |
| Host tests, ARM build, smoke check | L3 | verification | Recipe / Integration ordering |

Total plan length: under 500 lines.
