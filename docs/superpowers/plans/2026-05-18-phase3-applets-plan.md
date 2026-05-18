# Phase 3 Hemisphere applet ports - Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` to fan out implementer tasks in parallel. Use `superpowers:executing-plans` only if the user explicitly opts out of parallel execution.

**Goal:** Port 15 category-A Hemisphere applets into the disting NT compatibility shim, each with a host-side Catch2 test suite, executed in parallel from a single canonical recipe.

**Architecture:** One implementer subagent per applet, each in its own git worktree, each producing one commit. Single sequenced integration commit edits the alpha-ordered enum and factory tables. Pattern derived from 14 existing ports; recipe captured in the spec.

**Tech Stack:** C++17, vendor Phazerville source under `vendor/O_C-Phazerville` (SHA `7800d929`), disting NT runtime, Catch2 host harness, Make.

**Brainstorm:** `docs/superpowers/brainstorms/2026-05-17-phase3-applets-brainstorm.md`
**Spec:** `docs/superpowers/specs/2026-05-18-phase3-applets-design.md`

---

## Dependency declaration

Governing rule: `~/.claude/rules/parallel-execution.md`. Inlined substance for project readers:

- Default to parallel orchestration whenever subtasks are independent. Serialisation is the exception.
- A worklist of N items runs in parallel by default if: no cross-item data or ordering dependency, each item produces its own commit on its own branch, conflicts on shared files are limited to append regions, and each item is large enough to justify dispatch.
- Serialise a task only when (a) it depends on an earlier task's chosen abstractions or types, (b) it conflicts with an earlier task on the same logical region of a shared file in a non-appendable way, or (c) it requires an earlier task's output as input.

Applied to Phase 3:

- Tasks 1 through 15 (one applet port per task) are independent. Each implementer reads vendor source, writes its `pack_<applet>` helper (or skips if `OnDataRequest` returns 0), writes its TEST_CASEs, and commits on its own branch. The harness is unchanged; the spec recipe is unchanged.
- Append-region collisions across implementers exist but are mechanical:
  - `harness/tests/applet_test_helpers.h` and `.cpp`: each implementer appends declarations and definitions for its `pack_<applet>` helper. Order of declarations does not matter; the integrator concatenates additive content.
  - `harness/tests/test_hemispheres.cpp`: each implementer appends `using` directives, optional per-applet `<applet>_set(...)` helpers, and TEST_CASEs. Order does not affect semantics; the integrator concatenates.
- Task 16 (integration) is sequenced. It edits `shim/include/applet_indices.h` and `shim/include/HemispheresFactory.h` to register all 15 applets at once, in alphabetical order. Pure-append conflicts in the harness files are resolved here.
- Task 17 (final verification) is sequenced after Task 16.

End-to-end wall-clock target: approximately the time of the slowest implementer plus the integration commit plus the verification run. Not the sum of 15 implementer times.

## File structure

Files modified per implementer task (Tasks 1-15), in the implementer's worktree:

- Add: TEST_CASEs in `harness/tests/test_hemispheres.cpp`.
- Add: `pack_<applet>(...)` declaration in `harness/tests/applet_test_helpers.h` (skip if `OnDataRequest` returns 0).
- Add: `pack_<applet>(...)` implementation in `harness/tests/applet_test_helpers.cpp` (skip if `OnDataRequest` returns 0).
- No edits to `shim/`, `applets/Hemispheres.cpp`, or vendor source.

Files modified in the integration task (Task 16):

- `shim/include/applet_indices.h`: add 15 `kAppletXxx` enum values in alphabetical position before `kAppletCount`.
- `shim/include/HemispheresFactory.h`: add 15 entries each to the `#include` block, `applet_enum_strings()` `names[]`, `kMaxAppletSize` `cmax(...)` chain, `kMaxAppletAlign` `cmax(...)` chain, and `applet_factory()` `table[]`, all in alphabetical position.

## Implementer task shape (Tasks 1-15)

Each implementer task follows the same procedure. The narrative below applies to every applet; per-applet details are listed in the worklist below.

- [ ] Step 1: Spawn isolated worktree via `superpowers:using-git-worktrees` (the parent agent dispatches this; the implementer arrives in the worktree). Branch name: `claude/phase3-<applet-slug>`.
- [ ] Step 2: Read the spec entry for this applet at `docs/superpowers/specs/2026-05-18-phase3-applets-design.md`. Read the vendor header named in the entry, in full.
- [ ] Step 3: If the spec entry says `OnDataRequest` is non-empty, add the `pack_<applet>` declaration in `harness/tests/applet_test_helpers.h` and the implementation in `harness/tests/applet_test_helpers.cpp`, mirroring the bit layout from the spec entry. Apply all field biases and gap-bit guards exactly as the spec entry says.
- [ ] Step 4: Add TEST_CASEs in `harness/tests/test_hemispheres.cpp` covering at minimum: Start defaults, round-trip, and one case per major Controller branch as listed in the spec entry's "Test concerns" line. Phase 2 averaged 6 cases per applet; target a similar density.
- [ ] Step 5: Run `make test-applets` from the worktree root. The build will fail because `applet_indices.h` does not yet declare the new enum value. **Hand-edit `applet_indices.h` locally in the worktree** to add `kAppletXxx` immediately before `kAppletCount`. **Do not edit `HemispheresFactory.h`** in the implementer branch; the integration task owns that file. The local `applet_indices.h` edit will collide with Task 16; the integrator resolves by accepting Task 16's reordered enum.
- [ ] Step 6: Run `make test-applets`. Expected: all new cases pass, no regressions in existing cases.
- [ ] Step 7: Commit. Format: `feat(test-applets): <applet> <case-tag-list> <one-line behavioural summary>`. Example: `feat(test-applets): binary B1-B3 4-bit binary sum + count outputs`.

If the implementer's pack helper produces values that the vendor `OnDataReceive` clamps, the spec's round-trip case will read back the clamped values. That is correct behavior; mirror Cumulus CU5's commentary in the case.

## Worklist (Tasks 1-17)

Tasks listed alphabetically by applet name. All tasks are independent and run in parallel.

### Task 1: Binary

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Binary.h`.
- Category: A. Empty `OnDataRequest`; no pack helper.
- Spec entry: [Binary](../specs/2026-05-18-phase3-applets-design.md#binary).
- Analogue: Compare with GatedVCA's no-pack pattern.
- Commit: `feat(test-applets): binary BN1-BN4 sum + count + Gate/CV bit detect`.

### Task 2: ClockDivider

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ClockDivider.h`.
- Category: A. 32-bit pack helper, two per-channel +32-biased fields.
- Spec entry: [ClockDivider](../specs/2026-05-18-phase3-applets-design.md#clockdivider).
- Analogue: ClkToGate.
- Commit: `feat(test-applets): clock_divider CD1-CD4 per-side div + divmult round-trip`.

### Task 3: ClockSkip

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ClockSkip.h`.
- Category: A. 14-bit pack helper, two 7-bit percentage fields.
- Spec entry: [ClockSkip](../specs/2026-05-18-phase3-applets-design.md#clockskip).
- Analogue: Brancher.
- Commit: `feat(test-applets): clock_skip CS1-CS4 p=0/p=100 boundary + serialise`.

### Task 4: EnvFollow

- Vendor: `vendor/O_C-Phazerville/software/src/applets/EnvFollow.h`.
- Category: A. 16-bit pack helper, gain/duck/speed-1 layout.
- Spec entry: [EnvFollow](../specs/2026-05-18-phase3-applets-design.md#envfollow).
- Analogue: Slew.
- Commit: `feat(test-applets): env_follow EF1-EF4 peak tracking + duck + speed bias`.

### Task 5: PolyDiv

- Vendor: `vendor/O_C-Phazerville/software/src/applets/PolyDiv.h`.
- Category: A. 20-bit pack helper, 8-bit enable mask + two 6-bit divider steps.
- Spec entry: [PolyDiv](../specs/2026-05-18-phase3-applets-design.md#polydiv).
- Analogue: ClkToGate.
- Commit: `feat(test-applets): poly_div PD1-PD4 per-channel divider + enable mask`.

### Task 6: ProbabilityDivider

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ProbabilityDivider.h`.
- Category: A. 40-bit pack helper, four 4-bit weights + 8-bit loop_length + 16-bit seed.
- Spec entry: [ProbabilityDivider](../specs/2026-05-18-phase3-applets-design.md#probabilitydivider).
- Analogue: Brancher + Cumulus pack layout.
- Commit: `feat(test-applets): prob_divider PD1-PD5 weighted clock skip + seeded round-trip`.

### Task 7: ResetClock

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ResetClock.h`.
- Category: A. 17-bit pack helper, length-1 bias plus offset and spacing.
- Spec entry: [ResetClock](../specs/2026-05-18-phase3-applets-design.md#resetclock).
- Analogue: ClkToGate.
- Commit: `feat(test-applets): reset_clock RC1-RC4 length-1 bias + offset reset fire`.

### Task 8: RndWalk

- Vendor: `vendor/O_C-Phazerville/software/src/applets/RndWalk.h`.
- Category: A. 31-bit pack helper, six fields (1+4+8+8+8+2).
- Spec entry: [RndWalk](../specs/2026-05-18-phase3-applets-design.md#rndwalk).
- Analogue: Slew + Brancher.
- Commit: `feat(test-applets): rnd_walk RW1-RW4 bounded walk + seeded round-trip`.

### Task 9: RunglBook

- Vendor: `vendor/O_C-Phazerville/software/src/applets/RunglBook.h`.
- Category: A. 16-bit pack helper, single threshold field. **Test-step budget ceiling; see spec entry's "Test budget" line.**
- Spec entry: [RunglBook](../specs/2026-05-18-phase3-applets-design.md#runglbook).
- Analogue: Slew.
- Commit: `feat(test-applets): rungl_book RB1-RB3 threshold + 8-bit register walk`.

### Task 10: Schmitt

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Schmitt.h`.
- Category: A. 32-bit pack helper, two 16-bit fields (no bias).
- Spec entry: [Schmitt](../specs/2026-05-18-phase3-applets-design.md#schmitt).
- Analogue: Compare.
- Commit: `feat(test-applets): schmitt SM1-SM4 hysteresis + low/high round-trip`.

### Task 11: ShiftGate

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ShiftGate.h`.
- Category: A. 32-bit pack helper, gap-bit guard required at bits 10..15.
- Spec entry: [ShiftGate](../specs/2026-05-18-phase3-applets-design.md#shiftgate).
- Analogue: Cumulus (gap bits) + ClkToGate.
- Commit: `feat(test-applets): shift_gate SG1-SG4 register shift + gap bit guard`.

### Task 12: Stairs

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Stairs.h`.
- Category: A. 8-bit pack helper, three sub-byte fields. **10x clocked-multiplier caveat; see spec entry. Precedent: Cumulus CU2.**
- Spec entry: [Stairs](../specs/2026-05-18-phase3-applets-design.md#stairs).
- Analogue: Cumulus.
- Commit: `feat(test-applets): stairs ST1-ST4 stepped CV + dir + serialise`.

### Task 13: Switch

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Switch.h`.
- Category: A. Empty `OnDataRequest`; no pack helper.
- Spec entry: [Switch](../specs/2026-05-18-phase3-applets-design.md#switch).
- Analogue: GatedVCA / Button.
- Commit: `feat(test-applets): switch SW1-SW3 active-channel toggle + passthrough`.

### Task 14: Trending

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Trending.h`.
- Category: A. 16-bit pack helper, two 4-bit assigns + 8-bit sensitivity.
- Spec entry: [Trending](../specs/2026-05-18-phase3-applets-design.md#trending).
- Analogue: Compare.
- Commit: `feat(test-applets): trending TR1-TR4 slope fire + sensitivity round-trip`.

### Task 15: Voltage

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Voltage.h`.
- Category: A. 21-bit pack helper, gap-bit guard at bit 9.
- Spec entry: [Voltage](../specs/2026-05-18-phase3-applets-design.md#voltage).
- Analogue: AttenuateOffset.
- Commit: `feat(test-applets): voltage V1-V4 per-side CV + gate gate + gap bit guard`.

## Task 16: Integration

Sequenced. Runs after all 15 implementer tasks have committed on their isolated branches.

- [ ] Step 1: Create or check out the integration branch. Suggested: `dr/phase3-integration` or `claude/phase3-integration`.
- [ ] Step 2: Cherry-pick or merge all 15 implementer commits in alphabetical order by applet name. Resolve append-region conflicts in `harness/tests/applet_test_helpers.{h,cpp}` and `harness/tests/test_hemispheres.cpp` by concatenating additive content. Resolve `shim/include/applet_indices.h` conflicts by accepting all 15 enum inserts and reordering alphabetically.
- [ ] Step 3: Edit `shim/include/HemispheresFactory.h`. Insert in alphabetical position for each of the 15 applets:
  - `#include "<Applet>.h"` in the include block.
  - `"<short_name>"` in `applet_enum_strings()`' `names[]`. Match the existing 14 entries' short-name convention (max 10 chars, the same string the vendor `applet_name()` returns, abbreviated where needed).
  - `cmax(sizeof(<Applet>), ...)` entry in `kMaxAppletSize`. Same for `alignof(<Applet>)` in `kMaxAppletAlign`.
  - `&make_applet<<Applet>>` entry in `applet_factory()`' `table[]`.
- [ ] Step 4: Confirm `kAppletCount` is consistent across `applet_indices.h` and `HemispheresFactory.h`'s table sizes.
- [ ] Step 5: Run `make test-applets`. Expected: all 14 prior cases plus the new Phase 3 cases pass. Failures here mean integration ordering is wrong; do not push.
- [ ] Step 6: Commit. Format: `feat(applets): register 15 phase 3 applets + harness tests`. Include the full applet list in the commit body.

## Task 17: Final verification

Sequenced after Task 16.

- [ ] Step 1: Run `make test-applets`. Expected: full host suite passes, no failures.
- [ ] Step 2: Run `make arm`. Expected: ARM cross-build for the disting NT artefact succeeds with zero warnings.
- [ ] Step 3: Deploy the resulting plug-in to the disting NT hardware. Smoke-check at least 3 random Phase 3 applets selected in the Hemispheres pair:
  - Verify the applet selector lists all 15 new applets by name.
  - Select each smoke-check applet, exercise the front-panel UI (encoder, button, basic parameter changes), and verify outputs match expectations on an oscilloscope or via an audio probe.
- [ ] Step 4: If hardware smoke-check passes, push the integration branch and open a PR. PR description must include:
  - Summary: 15 category-A Hemisphere applets ported with host-side test coverage.
  - Test plan: `- [ ]` items naming each behaviour group exercised (sum + count detect, hysteresis, per-side divider, gap-bit round-trip, etc.). No generic items like "tests pass".
  - Link to the spec at `docs/superpowers/specs/2026-05-18-phase3-applets-design.md`.

## Spec coverage

| Applet | Task | Spec entry |
| --- | --- | --- |
| Binary | Task 1: Binary | [Binary](../specs/2026-05-18-phase3-applets-design.md#binary) |
| ClockDivider | Task 2: ClockDivider | [ClockDivider](../specs/2026-05-18-phase3-applets-design.md#clockdivider) |
| ClockSkip | Task 3: ClockSkip | [ClockSkip](../specs/2026-05-18-phase3-applets-design.md#clockskip) |
| EnvFollow | Task 4: EnvFollow | [EnvFollow](../specs/2026-05-18-phase3-applets-design.md#envfollow) |
| PolyDiv | Task 5: PolyDiv | [PolyDiv](../specs/2026-05-18-phase3-applets-design.md#polydiv) |
| ProbabilityDivider | Task 6: ProbabilityDivider | [ProbabilityDivider](../specs/2026-05-18-phase3-applets-design.md#probabilitydivider) |
| ResetClock | Task 7: ResetClock | [ResetClock](../specs/2026-05-18-phase3-applets-design.md#resetclock) |
| RndWalk | Task 8: RndWalk | [RndWalk](../specs/2026-05-18-phase3-applets-design.md#rndwalk) |
| RunglBook | Task 9: RunglBook | [RunglBook](../specs/2026-05-18-phase3-applets-design.md#runglbook) |
| Schmitt | Task 10: Schmitt | [Schmitt](../specs/2026-05-18-phase3-applets-design.md#schmitt) |
| ShiftGate | Task 11: ShiftGate | [ShiftGate](../specs/2026-05-18-phase3-applets-design.md#shiftgate) |
| Stairs | Task 12: Stairs | [Stairs](../specs/2026-05-18-phase3-applets-design.md#stairs) |
| Switch | Task 13: Switch | [Switch](../specs/2026-05-18-phase3-applets-design.md#switch) |
| Trending | Task 14: Trending | [Trending](../specs/2026-05-18-phase3-applets-design.md#trending) |
| Voltage | Task 15: Voltage | [Voltage](../specs/2026-05-18-phase3-applets-design.md#voltage) |
