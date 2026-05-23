# Brainstorm: Phase 3 Hemisphere applet ports (retry)

Date: 2026-05-18
Status: Draft (autonomous retry of 2026-05-17 brainstorm)
Owner: Dan
Worktree: `.worktrees/phase3-applets-plan`
Branch: `dr/phase3-applets-plan`
Prior attempt: `docs/superpowers/brainstorms/2026-05-17-phase3-applets-brainstorm.md`
Retrospective: `docs/superpowers/abort-reports/2026-05-18-phase3-attempt-1-retrospective.md`
ResetClock abort: `docs/superpowers/abort-reports/2026-05-18-resetclock-spec-mismatch.md`

## Vendor pin

- Submodule path: `vendor/O_C-Phazerville`
- Pinned SHA in `dr/phase3-applets-plan` tree: `7800d929f25868f9a8b7d3d50514532ee001649b`
- Unchanged from attempt 1. Inventory below is valid against this SHA.

Any re-pin invalidates the per-applet entries below. Rerun the brainstorm; do not patch it.

## Pair-applet decision

Retained from attempt 1. Phase 3 ports single-applet plug-ins only. Pair-applet variants (the `LogicCalculate` canary in commit `3b0c65b`) remain deferred. A future Phase 4 brainstorm can decide pair coverage.

## What changed since attempt 1

Four root causes from attempt 1 are addressed by this retry's documents (spec + plan):

1. Spec recipe was correct; per-entry derivations were not all verified against vendor `Controller()` and `Start()` semantics. Retry adds a mandatory per-entry verification footer in the spec.
2. Parent agent dispatched implementer worktrees from `main` instead of `dr/phase3-applets-plan`. Retry plan inlines the worktree-dispatch checklist from `~/.claude/rules/parallel-execution.md`.
3. Implementer contract was enforced via prose; 40% of successful implementers violated it. Retry plan installs a pre-commit hook in each worktree that rejects forbidden-surface commits.
4. Recipe did not name the 10x clocked-multiplier rule. Retry spec adds a Recipe subsection covering it, with Cumulus CU2 as the precedent and ProbabilityDivider as the redesign template.

Attempt 1 also produced 6 clean test commits at `refs/archive/phase3-attempt-1/<slug>`. These are reference material, not load-bearing.

## Cherry-pick versus re-run

The 6 archived clean commits are: `clock_skip`, `clock_divider`, `rnd_walk`, `schmitt`, `stairs`, `switch`. All predate the 10x-multiplier rule and the per-entry verification footer. The retry policy is consistent across all applets: **re-run by default; cherry-pick only if the retry spec entry explicitly confirms no test-body change is needed**.

Per-applet disposition (recorded in retry spec):

- `clock_skip`: re-run. No 10x risk per audit, but new spec entry may revise round-trip seed handling.
- `clock_divider`: re-run. Attempt 1 implementer found CD2/CD3 cannot distinguish `div=2` from `div=4` at the bus boundary. Spec entry rewrites tests to model 10x explicitly.
- `rnd_walk`: re-run. No 10x risk, but archived test body has not been re-validated against retry spec field claims.
- `schmitt`: re-run. No 10x risk. Tests are simple but consistency policy applies.
- `stairs`: re-run. 10x handled correctly in archive. New spec entry retains the Cumulus CU2 precedent reference; re-run keeps consistency.
- `switch`: re-run. Spec entry needs `active[]` semantics correction and SW2 redesign (10x defeated original case).

Net: all 11 in-scope applets are re-runs. Zero cherry-picks. Decision rationale: consistency outweighs the small effort of re-running 6 clean test bodies; new spec entries may diverge in field names, observable claims, or RNG seeding, and a uniform re-run policy makes per-entry verification straightforward.

## Status per applet

15 in-scope applets from attempt 1's set. Status decided per:

- **In scope for retry**: test approach is sound (possibly after redesign covered in spec); shim prereqs are present (icon stub additions handled by integration task) or trivially addable; no remaining spec defect after retry spec.
- **Deferred pending shim work**: needs shim infrastructure not yet present (macro, member method, helper class, segment display).
- **Deferred pending test redesign**: not used in this retry; redesign is folded into in-scope-for-retry under the new 10x rule.
- **Deferred pending spec rewrite**: spec rewrite scope exceeds a simple revision; full Phase 4 effort.

| # | Applet | Category | Status | Cherry-pick? | Notes |
| --- | --- | --- | --- | --- | --- |
| 1 | Binary | A | Deferred pending shim work | n/a | Needs SegmentDisplay shim, `HEMISPHERE_3V_CV` macro, `binaryCounter` icon. Three independent shim prereqs. |
| 2 | ClockDivider | A | In scope for retry | re-run | 10x affected. Spec entry models multiplier in tests (Cumulus CU2 precedent). |
| 3 | ClockSkip | A | In scope for retry | re-run | RNG-driven probabilistic gate. No 10x risk. |
| 4 | EnvFollow | A | In scope for retry | re-run | Continuous CV reading; envelope settling time. No 10x risk. |
| 5 | PolyDiv | A | In scope for retry | re-run | 10x affected. Spec entry rewrites for 4-channel `div_enabled` (not 2) and models multiplier. |
| 6 | ProbabilityDivider | A | In scope for retry | re-run | 10x affected. Spec restricts to round-trip + state-injection only; no bus-level fire-count assertions. |
| 7 | ResetClock | A | Deferred pending spec rewrite | n/a | Per `2026-05-18-resetclock-spec-mismatch.md`: requires full rewrite anchored on Burst analogue. Demoted to Phase 4. |
| 8 | RndWalk | A | In scope for retry | re-run | RNG-driven walk. No 10x risk. |
| 9 | RunglBook | A | In scope for retry | re-run | 8-bit shift register. 10x folded into expected register state (10 shifts per buffer). |
| 10 | Schmitt | A | In scope for retry | re-run | Per-side hysteresis. No 10x risk. |
| 11 | ShiftGate | A | Deferred pending shim work | n/a | Needs `HEMISPHERE_3V_CV` macro at shim layer. Shim port is one-line but out of retry scope. |
| 12 | Stairs | A | In scope for retry | re-run | 10x affected. Spec retains Cumulus CU2 precedent reference. |
| 13 | Switch | A | In scope for retry | re-run | Empty `OnDataRequest`. Spec corrects `active[]` to `{1, 2}` semantics. SW2 redesigned for 10x. |
| 14 | Trending | A | Deferred pending shim work | n/a | Needs `HemisphereApplet::Changed(int ch)` member method at shim layer. |
| 15 | Voltage | A | In scope for retry | re-run | Gap-bit guard at position 9. No 10x risk. |

**Retry scope: 11 in-scope-for-retry. 4 demotions** (Binary, ResetClock, ShiftGate, Trending). 11 ≥ 10 (abort threshold not triggered).

Cherry-pick distribution: 0 of 6 archived clean commits cherry-picked; all 11 in-scope applets are re-runs.

## Out-of-scope category list

Unchanged from attempt 1. Categories E (quantizer), F (scales), G (MIDI), H (audio DSP), I (system/hardware/full-screen) remain deferred. See attempt 1 brainstorm sections "Out of scope - category E/F/G/H/I" for the canonical exclusion list.

## Phase 3 retry success criterion

Land 11 category-A applet ports with test coverage on `dr/phase3-applets-plan`. The 4 demoted applets remain in `vendor/O_C-Phazerville` unported; they will be re-attempted in Phase 4 after their shim prereqs land or their specs are rewritten.

## References

- Primary code (recipe ground truth): `applets/Hemispheres.cpp`, `shim/include/applet_indices.h`, `shim/include/HemispheresFactory.h`, `shim/include/PhzIcons.h`, `harness/tests/applet_test_helpers.{h,cpp}`, `harness/tests/test_hemispheres.cpp`.
- Attempt 1 docs: see header.
- Project rule: `~/.claude/rules/parallel-execution.md` (worktree-dispatch checklist, recovery procedure).
- Retry spec: `docs/superpowers/specs/2026-05-18-phase3-retry-design.md`.
- Retry plan: `docs/superpowers/plans/2026-05-18-phase3-retry-plan.md`.
