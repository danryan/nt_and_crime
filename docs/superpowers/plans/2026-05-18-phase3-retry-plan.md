# Plan: Phase 3 Hemisphere applet ports (retry)

Date: 2026-05-18
Status: Active (autonomous retry of 2026-05-18 plan)
Owner: Dan
Brainstorm: `docs/superpowers/brainstorms/2026-05-18-phase3-retry-brainstorm.md`
Spec: `docs/superpowers/specs/2026-05-18-phase3-retry-design.md`
Branch: `dr/phase3-applets-plan`
Worktree: `.worktrees/phase3-applets-plan`

## Goal

Land tests for 11 category-A Hemisphere applet ports on `dr/phase3-applets-plan`. Open a PR. Four applets remain deferred to Phase 4: Binary, ResetClock, ShiftGate, Trending.

## Dependency declaration

This plan executes under `~/.claude/rules/parallel-execution.md`. The Worktree-dispatch checklist section (lines 59-77) and the recovery procedure section are prerequisites. The substance is inlined below so this plan is auditable without reference to the personal rules file.

Default is parallel. A task serializes only when (a) a later item structurally depends on an earlier item's chosen abstractions or types, (b) items modify the same logical region in incompatible ways, or (c) the task is small enough that orchestration overhead exceeds the parallel gain.

In this plan: the 11 implementer tasks are independent and execute in parallel (one isolated worktree per applet). The integration task is sequenced after all implementer commits land. Final verification is sequenced after integration.

End-to-end wallclock target: roughly the time of the slowest single implementer task plus integration plus verification.

## Worktree-dispatch checklist (parent agent's responsibility)

Before dispatching any implementer subagent, the parent agent runs these steps per implementer worktree. The substance is inlined; the canonical text lives in `~/.claude/rules/parallel-execution.md`.

1. **Specify the base branch explicitly.** `git worktree add <path> -b <new-branch> dr/phase3-applets-plan`. Never rely on the worktree tool's default base. The retry spec and plan live on `dr/phase3-applets-plan`, not on `main`.
2. **Verify the spec docs are reachable inside the new worktree before the subagent launches.** Run `test -f <worktree>/docs/superpowers/specs/2026-05-18-phase3-retry-design.md && test -f <worktree>/docs/superpowers/plans/2026-05-18-phase3-retry-plan.md`. If either is missing, the dispatch is broken; abort it.
3. **Verify submodules are initialized inside the new worktree.** Run `git -C <worktree> submodule update --init --depth=1 vendor/O_C-Phazerville`. The implementer reads vendor source; worktrees do not inherit submodule state.
4. **Install the pre-commit hook** specified in the next section. Verify it is executable.
5. **State the implementer's allowed and forbidden file surface explicitly** in the dispatch prompt. The hook enforces it; the prose names it.

## Pre-commit hook

The parent agent installs this hook in each implementer worktree at `.git/worktrees/<applet>/hooks/pre-commit` and `chmod +x` it. The hook lives outside the working tree (in the worktree's git common dir), so it does not show up in `git diff` and the implementer cannot accidentally commit a modified hook.

```sh
#!/bin/sh
# Phase 3 retry implementer pre-commit hook.
# Rejects commits that stage forbidden-surface changes or run from a
# worktree branched from the wrong base.

set -e

# Forbidden surface for Phase 3 retry implementers.
forbidden='shim/include/applet_indices.h shim/include/HemispheresFactory.h shim/include/PhzIcons.h shim/include/HemisphereApplet.h shim/include/HSUtils.h shim/include/hemispheres_shim.h'

staged=$(git diff --cached --name-only)

for f in $forbidden; do
    if echo "$staged" | grep -qx "$f"; then
        echo "pre-commit: refusing to commit forbidden-surface file: $f" >&2
        echo "pre-commit: integration task owns this file; remove from the commit." >&2
        exit 1
    fi
done

# Verify the worktree's branch derives from dr/phase3-applets-plan, not main.
branch=$(git rev-parse --abbrev-ref HEAD)
if ! git merge-base --is-ancestor dr/phase3-applets-plan HEAD 2>/dev/null; then
    echo "pre-commit: refusing to commit on branch '$branch' that is not derived from dr/phase3-applets-plan." >&2
    echo "pre-commit: re-create the worktree with: git worktree add <path> -b <branch> dr/phase3-applets-plan" >&2
    exit 1
fi

exit 0
```

The hook is installed by the parent agent during worktree creation. The implementer never edits this hook. If the hook rejects a commit, the implementer un-stages the forbidden file and continues; the integration task picks up the shim infra edits.

## File structure

Each implementer task produces edits to exactly these files in the implementer's worktree:

- `harness/tests/applet_test_helpers.h` - one `uint64_t pack_<applet>(...)` declaration in `namespace hem_test`. Skip for Switch (empty `OnDataRequest`).
- `harness/tests/applet_test_helpers.cpp` - one `pack_<applet>` implementation. Skip for Switch.
- `harness/tests/test_hemispheres.cpp` - `using hem_shim::kApplet<Name>;`, optional `<applet>_set(hi, ...)` helper, N `TEST_CASE`s under tag `[<applet>]`.

Forbidden in implementer commits (enforced by hook):

- `shim/include/applet_indices.h`, `shim/include/HemispheresFactory.h`, `shim/include/PhzIcons.h`, `shim/include/HemisphereApplet.h`, `shim/include/HSUtils.h`, `shim/include/hemispheres_shim.h`.

Owned by the integration task on `dr/phase3-applets-plan` after all implementer commits land:

- `shim/include/applet_indices.h` - 11 new `kApplet<Name>` enum entries, alpha-ordered before `kAppletCount`.
- `shim/include/HemispheresFactory.h` - 55 new entries (5 per applet x 11 applets): includes, names entries, two `cmax` chain entries, factory table entries.
- `shim/include/PhzIcons.h` - 11 new `extern const uint8_t <icon>[8];` stubs.

The implementer cannot validate the test syntax through a full build because `applet_indices.h` lacks the new enum entry. The implementer compiles the test file in isolation using the project's per-test compile rule. The Step 5 verification is name resolution and syntax only; full test execution lands in the integration task.

## Implementer task shape

Each implementer task is one paragraph stating: vendor file, category, link to the spec entry, named analogue, cherry-pick disposition, expected commit message. The implementer reads the spec entry and produces the pack helper plus test cases per the recipe.

No paste-verbatim test bodies appear in this plan. The pattern lives in the spec recipe.

If the implementer discovers a spec defect mid-task (vendor `Controller()` contradicts the spec entry's observables claim, missing shim helper, OnDataReceive constraint not declared in the spec), the implementer aborts with `BLOCKED` status and reports the defect. The parent agent revises the spec entry before re-dispatch.

## Implementer tasks

### Task 1: ClockDivider

Vendor: `vendor/O_C-Phazerville/software/src/applets/ClockDivider.h:23-157`. Spec entry: `docs/superpowers/specs/2026-05-18-phase3-retry-design.md#clockdivider`. Analogue: ClkToGate. Cherry-pick: re-run. 10x affected; tests model the multiplier explicitly. Commit: `feat(test-applets): clock_divider CD1-CDn divisor + multiplier under 10x`.

### Task 2: ClockSkip

Vendor: `vendor/O_C-Phazerville/software/src/applets/ClockSkip.h`. Spec entry: spec `#clockskip`. Analogue: Brancher. Cherry-pick: re-run. No 10x risk. Commit: `feat(test-applets): clock_skip CS1-CSn probabilistic gate pass`.

### Task 3: EnvFollow

Vendor: `vendor/O_C-Phazerville/software/src/applets/EnvFollow.h`. Spec entry: spec `#envfollow`. Analogue: Slew. Cherry-pick: re-run. No 10x fire-count risk. Commit: `feat(test-applets): env_follow EF1-EFn envelope tracking + duck`.

### Task 4: PolyDiv

Vendor: `vendor/O_C-Phazerville/software/src/applets/PolyDiv.h`. Spec entry: spec `#polydiv`. Analogue: ClkToGate. Cherry-pick: re-run. 10x affected (4 parallel dividers); spec corrects 4-channel iteration. State-injection coverage; no bus fire-count claim. Commit: `feat(test-applets): poly_div PD1-PDn four-channel divider under 10x`.

### Task 5: ProbabilityDivider

Vendor: `vendor/O_C-Phazerville/software/src/applets/ProbabilityDivider.h`. Spec entry: spec `#probabilitydivider`. Analogue: Brancher + Cumulus. Cherry-pick: re-run. 10x affected; round-trip plus state-injection only, no bus-level fire-count assertions. Commit: `feat(test-applets): prob_div PR1-PRn weighted skip round-trip + state`.

### Task 6: RndWalk

Vendor: `vendor/O_C-Phazerville/software/src/applets/RndWalk.h`. Spec entry: spec `#rndwalk`. Analogue: Slew + Brancher. Cherry-pick: re-run. 10x present but amplitude-bounded; bus-level bounded-amplitude assertion remains reliable. Commit: `feat(test-applets): rnd_walk RW1-RWn random walk + smoothness`.

### Task 7: RunglBook

Vendor: `vendor/O_C-Phazerville/software/src/applets/RunglBook.h`. Spec entry: spec `#runglbook`. Analogue: Slew. Cherry-pick: re-run. 10x folded into expected register state (RB2 precedent). Commit: `feat(test-applets): rungl_book RB1-RBn 8-bit register threshold + freeze`.

### Task 8: Schmitt

Vendor: `vendor/O_C-Phazerville/software/src/applets/Schmitt.h`. Spec entry: spec `#schmitt`. Analogue: Compare. Cherry-pick: re-run. No 10x risk. Commit: `feat(test-applets): schmitt SC1-SCn hysteresis comparator`.

### Task 9: Stairs

Vendor: `vendor/O_C-Phazerville/software/src/applets/Stairs.h`. Spec entry: spec `#stairs`. Analogue: Cumulus. Cherry-pick: re-run. 10x present; tests model the multiplier explicitly (Cumulus CU2 precedent). Commit: `feat(test-applets): stairs ST1-STn stepped CV up/down under 10x`.

### Task 10: Switch

Vendor: `vendor/O_C-Phazerville/software/src/applets/Switch.h:23-104`. Spec entry: spec `#switch`. Analogue: GatedVCA / Button. Cherry-pick: re-run. Empty `OnDataRequest`; no pack helper. `active[]={1,2}` semantics corrected. 10x defeats the Clock(0) toggle case; tests use `Gate(1)` on channel-1 switch instead. Commit: `feat(test-applets): switch SW1-SWn channel-1 gated select + active[] semantics`.

### Task 11: Voltage

Vendor: `vendor/O_C-Phazerville/software/src/applets/Voltage.h:22-134`. Spec entry: spec `#voltage`. Analogue: AttenuateOffset. Cherry-pick: re-run. Gap-bit guard at position 9. No 10x risk. Commit: `feat(test-applets): voltage VL1-VLn constant CV + gate polarity`.

## Implementer task execution (Step-by-step)

These steps run inside each implementer worktree. The parent agent dispatches one implementer subagent per task; all 11 dispatches happen in a single parallel batch.

- [ ] **Step 0: Verify worktree location and branch.**

```bash
pwd
git rev-parse --abbrev-ref HEAD
git merge-base --is-ancestor dr/phase3-applets-plan HEAD && echo "branch derives from dr/phase3-applets-plan" || (echo "WRONG BASE - abort" && exit 1)
test -f docs/superpowers/specs/2026-05-18-phase3-retry-design.md || (echo "spec missing - abort" && exit 1)
```

- [ ] **Step 1: Read the spec entry.**

Read `docs/superpowers/specs/2026-05-18-phase3-retry-design.md` and find the entry for the assigned applet. Read the recipe section in full.

- [ ] **Step 2: Read the vendor header.**

Read `vendor/O_C-Phazerville/software/src/applets/<Applet>.h` end-to-end. Cross-check the spec entry's `OnDataRequest`, `Start()`, and `Controller()` claims against the source. If anything contradicts, abort with `BLOCKED`.

- [ ] **Step 3: Write the pack helper (skip for empty-`OnDataRequest`).**

Add declaration in `harness/tests/applet_test_helpers.h`. Add implementation in `harness/tests/applet_test_helpers.cpp`. Follow the pack helper signature and rules from the spec recipe. Zero gap bits explicitly when the vendor `OnDataRequest` has them.

- [ ] **Step 4: Write the test cases.**

Add `using hem_shim::kApplet<Name>;` near the top of `harness/tests/test_hemispheres.cpp`. Add the optional per-applet `<applet>_set(hi, ...)` helper if the test cases need state injection. Add N `TEST_CASE`s under tag `[<applet>]` per the spec entry's test concerns. Each `TEST_CASE` uses the standard fixture pattern (`setup_applet`, `clear_bus`, `set_gate`/`hold_gate`/`set_cv`, `step_n_frames`, `read_gate_at`/`read_cv_at`).

- [ ] **Step 5: Validate test compilation in isolation.**

The full `make test-applets` will fail because `applet_indices.h` lacks the new enum entry. Validate the test file's syntax by compiling `harness/tests/test_hemispheres.cpp` alone with the project's per-source compile rule, ignoring the linker step. The implementer does not edit `applet_indices.h` even temporarily. The pre-commit hook rejects any commit that touches it.

Specifically: run `make -n test_hemispheres.o` to show the compile command, run that command directly, and confirm zero compile errors. Linker errors on `kApplet<Name>` are expected at this stage; they resolve at integration.

- [ ] **Step 6: Commit.**

```bash
git add harness/tests/applet_test_helpers.h harness/tests/applet_test_helpers.cpp harness/tests/test_hemispheres.cpp
git diff --cached --name-only
git commit -m "<commit message from the task line>"
```

The pre-commit hook validates the staged surface and the branch base. If the hook rejects, un-stage the forbidden file and re-commit.

- [ ] **Step 7: Report status.**

Report to the parent agent: worktree path, branch name, commit SHA, test compile output (zero errors expected from Step 5), any deviations from the spec entry that the implementer applied (with vendor source citations), and final status (`DONE`, `DONE_WITH_CONCERNS`, `NEEDS_CONTEXT`, or `BLOCKED`).

## Integration task (sequenced after all implementer commits)

- [ ] **Step I1: Verify all 11 implementer commits exist.**

```bash
git branch -a | grep 'phase3-retry/'
```

11 implementer branches expected. If fewer than 11 succeeded, surface the abort to the user.

- [ ] **Step I2: Merge implementer commits onto `dr/phase3-applets-plan`.**

Cherry-pick each implementer commit. Conflicts on `harness/tests/applet_test_helpers.{h,cpp}` and `harness/tests/test_hemispheres.cpp` are resolved by additive concatenation: each implementer's `pack_<applet>` and `TEST_CASE` block is appended in alphabetical order. The conflict surface is append-only because each implementer adds new entries at the end of the file; non-append conflicts indicate an implementer violated the contract and should be surfaced as an abort.

- [ ] **Step I3: Add the 11 enum entries to `shim/include/applet_indices.h`.**

Insert `kApplet<Name>` for each in-scope applet at the alpha-ordered position before `kAppletCount`.

- [ ] **Step I4: Add the 55 registration entries to `shim/include/HemispheresFactory.h`.**

For each applet, insert in alphabetical position:

- `#include "<Applet>.h"` in the include block.
- The display name string in `applet_enum_strings()` `names[]`.
- `sizeof(<Applet>)` in the `kMaxAppletSize` `cmax(...)` chain.
- `alignof(<Applet>)` in the `kMaxAppletAlign` `cmax(...)` chain.
- `&make_applet<<Applet>>` in `applet_factory()` `table[]` at the index matching the enum order.

- [ ] **Step I5: Add the 11 icon stubs to `shim/include/PhzIcons.h`.**

For each applet, add `extern const uint8_t <iconVar>[8];`. The icon variable name matches the vendor `applet_icon()` return value (`PhzIcons::clockDivider`, `PhzIcons::clockSkip`, etc.). Define corresponding empty stubs in `shim/PhzIcons.cpp` if the icon definitions are not picked up from vendor source.

- [ ] **Step I6: Commit the integration entries.**

```bash
git add shim/include/applet_indices.h shim/include/HemispheresFactory.h shim/include/PhzIcons.h shim/PhzIcons.cpp
git commit -m "feat(shim): register Phase 3 retry applets (ClockDivider, ClockSkip, EnvFollow, PolyDiv, ProbabilityDivider, RndWalk, RunglBook, Schmitt, Stairs, Switch, Voltage)"
```

## Final verification

- [ ] **Step V1: Run host tests.**

```bash
make test-applets
```

Expected: all existing Phase 1 and Phase 2 tests pass; all 11 retry applet test suites pass; zero failures.

If any existing test regresses, the integration is wrong; do not push. If any new retry applet test fails, the implementer commit is wrong or the spec entry is wrong; fix and re-integrate.

- [ ] **Step V2: Run the ARM build.**

```bash
make arm
```

Expected: zero warnings, exit code 0.

- [ ] **Step V3: Hardware smoke check.**

Deploy the built plug-in to the disting NT. Load three applets covering distinct recipe variations:

- One empty-`OnDataRequest` applet: Switch.
- One gap-bit applet: Voltage.
- One RNG-using applet: ClockSkip.

For each, set a non-default state via the disting NT UI, save the preset, reload it, and confirm the saved state is restored. Drive Clock(0) and Clock(1) inputs and confirm the outputs match the test cases' expectations on the hardware.

If hardware behavior disagrees with the host-test expectations, surface the discrepancy with vendor source citations.

## Pre-PR quality gate

Before opening the PR:

- All 11 implementer commits land on `dr/phase3-applets-plan`.
- Integration commit lands on `dr/phase3-applets-plan`.
- `make test-applets` passes.
- `make arm` succeeds with zero warnings.
- Hardware smoke check passes for Switch, Voltage, and ClockSkip.

## PR

Open a PR from `dr/phase3-applets-plan` to the default branch. PR description includes:

- Summary: 11 category-A applet ports added; 4 deferred to Phase 4 with reasons.
- Test plan: markdown checkbox list naming each applet's test cases.
- Load-bearing decisions: retry scope (11 in-scope, 4 deferred); cherry-pick disposition per applet (all 11 re-run); per-entry verification findings (0 of 3 audit entries contradicted vendor reality); spec entries with revisions from attempt 1 (ClockDivider, PolyDiv, ProbabilityDivider, Stairs, Switch); implementer outcomes (per-implementer SHAs and any deviations applied); pre-commit hook content and rationale.

## Spec coverage

| Applet | Task | Spec entry |
| --- | --- | --- |
| ClockDivider | Task 1: ClockDivider | spec `#clockdivider` |
| ClockSkip | Task 2: ClockSkip | spec `#clockskip` |
| EnvFollow | Task 3: EnvFollow | spec `#envfollow` |
| PolyDiv | Task 4: PolyDiv | spec `#polydiv` |
| ProbabilityDivider | Task 5: ProbabilityDivider | spec `#probabilitydivider` |
| RndWalk | Task 6: RndWalk | spec `#rndwalk` |
| RunglBook | Task 7: RunglBook | spec `#runglbook` |
| Schmitt | Task 8: Schmitt | spec `#schmitt` |
| Stairs | Task 9: Stairs | spec `#stairs` |
| Switch | Task 10: Switch | spec `#switch` |
| Voltage | Task 11: Voltage | spec `#voltage` |

Deferred applets (Binary, ResetClock, ShiftGate, Trending) appear in the spec under "Deferred entries" with a one-line block per applet stating the prereq.
