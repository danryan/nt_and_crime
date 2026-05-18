# Phase 5: Land time-injection harness helper + cat-C time-sensitive applets (autonomous)

Phase 4 shipped 7 Hemisphere applet ports on `main` at squash commit `c68d6bc`: Binary, ShiftGate, Trending, ProbabilityDivider (4 Phase 3 deferrals) plus ADEG, ADSREG, GameOfLife (3 cat-B). ResetClock was demoted to Phase 5 because it requires a category-C time-injection helper not in Phase 4's bounded scope.

The Phase 4 brainstorm at `docs/superpowers/brainstorms/2026-05-18-phase4-applets-brainstorm.md` enumerates Phase 5 carry-forward:

- ResetClock (needs cat-C time-injection helper).
- Category-C time-sensitive applets blocked on the same helper: EbbAndLfo, LowerRenz, VectorEG, VectorLFO, VectorMod, VectorMorph, Relabi, Shuffle, Xfader.
- CVInputMap shim port (unblocks Combin8).
- MIDIQuantizer / scales port (unblocks cat-E/F bucket).
- Category-D stateful sequencers harness pattern.

Phase 5 focuses on the **time-injection helper** as the primary deliverable plus the applets it unblocks. CVInputMap, MIDIQuantizer, and cat-D sequencers are explicitly deferred to Phase 6.

## Phase 5 scope is bounded

Phase 5 ships the time-injection harness helper (Layer 0) plus ResetClock (the load-bearing test target) plus 5-8 cat-C applets selected by named criteria. Total Phase 5 target: 6-9 applets plus 1 harness helper.

The bounded scope is deliberate. Phase 4 shipped 7 applets without parallel subagent dispatch (load-bearing decision documented in the merged PR). Phase 5 introduces new harness complexity (time-injection) and must validate it at proven scale before broader expansion.

## The two most important rules

These two rules are repeated because the Phase 3 attempt 1 retrospective traces the entire failure to violating them:

1. **PARALLELIZE INDEPENDENT WORK.** Default to parallel. The plan must explicitly declare which tasks are independent and dispatch them in a single parallel batch (multiple `Agent` tool calls in one message). End-to-end wallclock target is roughly the time of the slowest single task plus integration, not the sum of all tasks. Serial dependencies are the exception, not the rule. If the parent agent collapses Layer 1 + Layer 2 into an inline workflow (as Phase 4 did), document the load-bearing decision and justification explicitly; that pattern is allowed when section markers provide isolation.

2. **EVERY IMPLEMENTER WORKTREE BRANCHES FROM `dr/phase5-applets-plan`, NOT FROM `main`.** The parent agent must specify the base branch explicitly: `git worktree add <path> -b <implementer-branch> dr/phase5-applets-plan`. Never rely on the default.

## Model selection

- **Parent orchestrator**: `opus` (this is what you are).
- **Implementer subagents (if dispatched)**: `sonnet`.
- **Audit/Explore subagents**: `sonnet`.

## Autonomous execution contract

The agent runs through the following phases without pausing for user review except at the preflight checkpoint:

1. Audit + verification (read primary references, verify rule prerequisites, audit cat-C applet inventory, design time-injection helper).
2. **Preflight checkpoint** (post a one-paragraph preflight report, proceed unless an abort condition fired during audit).
3. Phase 5 brainstorm.
4. Phase 5 spec.
5. Phase 5 plan.
6. Layer 0: time-injection harness helper + any cat-C shim prereqs.
7. Layer 0.5: section markers in test files.
8. Layer 1: applet implementation (parallel or inline per load-bearing decision).
9. Layer 2: integration.
10. Layer 3: final verification (host tests, `make arm`, hardware smoke check).
11. PR open with Load-bearing decisions section.

## Branch context

Phase 5 runs on a NEW feature branch `dr/phase5-applets-plan` created from current `main` (which now includes Phase 4 at squash commit `c68d6bc`).

Step 1: `git worktree add .worktrees/phase5-applets-plan -b dr/phase5-applets-plan main`. Initialize submodules in the new worktree: `git -C .worktrees/phase5-applets-plan submodule update --init --recursive --depth=1`.

## Worktree naming

- Parent worktree: `.worktrees/phase5-applets-plan` on branch `dr/phase5-applets-plan`.
- Implementer worktrees: `.worktrees/phase5-port-<slug>` on branch `phase5-port/<slug>`.
- Optional shim worktrees: `.worktrees/phase5-shim-<slug>` on branch `phase5-shim/<slug>`.
- Layer 0 + Layer 0.5 + Layer 2 commits land directly on `dr/phase5-applets-plan`.

## Pre-commit hook

Phase 4 plan's pre-commit hook is the template. Update branch-name patterns to accept commits on:

- `dr/phase5-applets-plan` (Layer 0, Layer 0.5, Layer 2).
- `phase5-port/*` (Layer 1, forbidden surface enforced).
- `phase5-shim/*` (optional isolated shim, forbidden surface relaxed for `shim/` paths).

Reject commits on `main`, on any branch not derived from `dr/phase5-applets-plan`, and any `phase5-port/*` commit that stages a forbidden-surface file.

## Required skills + rules

- `superpowers:using-git-worktrees`. Step zero.
- `superpowers:brainstorming`. Drives the brainstorm.
- `superpowers:writing-plans`. Drives spec + plan.
- `superpowers:subagent-driven-development` (if dispatching Layer 1 in parallel).

Load + honor `~/.claude/rules/parallel-execution.md`. Inline the worktree-dispatch checklist into the plan so it is auditable without the personal rules file.

## Time-injection helper design

The shim runs the vendor `Controller()` 10 times per `step()` call (`ticks_this_step = numFrames / 3 = 10`). For time-sensitive applets, advancing wall-clock state (`OC::CORE::ticks`, internal countdowns) requires either many step()s (expensive, brittle) or a direct injection seam.

The Phase 5 helper must:

1. Provide a way for tests to advance `OC::CORE::ticks` by N inner ticks without driving `step_n_frames(N * 10)`. The helper either calls `Controller()` directly N times with the current bus state, or exposes a way to bump `ticks` and drain countdowns without re-entering the bus I/O layer.
2. Honor the 10x clocked multiplier rule. The helper does NOT bypass `clocked[ch]` state; it preserves it across injected ticks so applets see the same `Clock(ch)` semantics they would under `step_n_frames`.
3. Preserve `frame.clock_countdown[]` and `frame.adc_lag_countdown[]` decrement timing. If the helper runs N inner ticks, the countdowns decrement N times.
4. Have a clear naming convention: `step_n_inner_ticks(loaded, alg, bus, N)` mirrors `step_n_frames` but expresses inner ticks directly. The helper documents that 1 inner tick is NOT 1 step; 1 inner tick is `1/10` of a step.

Design alternative the brainstorm must evaluate: instead of a new helper, expose a way to set `ticks_this_step` per-step (e.g., a global the test sets before `step()`). Tradeoffs surface in the brainstorm.

The Layer 0 commit lands the helper + a unit test verifying the helper produces equivalent state to `step_n_frames` for a known applet (Cumulus is a good probe since its 10x behavior is already covered).

## ResetClock spec rewrite specifics

The ResetClock spec rewrite MUST cite all five facts from `docs/superpowers/abort-reports/2026-05-18-resetclock-spec-mismatch.md`:

a. **Clock(1) is the external Reset input, not Clock(0)**.
b. **Reset fires on `pending_clocks == 1` at output time**, not `position == offset`.
c. **`Start()` does not reset `offset_mod`**.
d. **First-tick `UpdateOffset` jumps `pending_clocks` by the offset value**.
e. **Closer analogue is Burst** (pending-pulse queue with `RC_TICKS_PER_MS = 175` timing), not ClkToGate.

ResetClock test concerns rewrite the attempt-1 cases against the corrected semantics. Spec entry traces these facts to the abort report with `file:line` citations.

The time-injection helper makes ResetClock testable. spacing=6 * RC_TICKS_PER_MS=175 / ticks_this_step=10 = 105 step() calls per output cycle becomes ~10 helper calls advancing 10 inner ticks each, or one helper call advancing 1050 inner ticks. Tests express timing in inner ticks instead of step() calls.

## Operational category-C boundary

Phase 5 brainstorm MUST define the cat-C boundary with concrete predicates. Draft predicates:

1. **Vendor predicate**: Controller reads `OC::CORE::ticks`, `millis()`, `micros()`, or compares against an internal tick counter that gates state transitions.
2. **Required harness change**: the time-injection helper from Layer 0. If the applet needs additional infrastructure beyond what the helper provides, defer.
3. **No quantizer/MIDI/audio**: same categorical exclusions as Phase 4.
4. **Bounded state evolution**: state machine transitions are deterministic given known input + tick budget. No external nondeterminism beyond seeded RNG.

The brainstorm picks 5-8 cat-C applets by named criteria in priority order. Default order:

1. No RNG in Controller.
2. OnDataRequest packed in <=40 bits.
3. Controller body <60 lines.
4. Closest analogue is an already-ported applet.
5. State evolution observable at the bus boundary within reasonable tick budget.

## Phase 5 in-scope candidates (audit confirms which fit)

- **ResetClock** (`ResetClock.h:24-192`): the load-bearing test target for the helper.
- **EbbAndLfo** (`EbbAndLfo.h`): time-based phase accumulator LFO.
- **LowerRenz** (`LowerRenz.h`): chaos generator with internal state evolution.
- **VectorLFO, VectorMod, VectorMorph, VectorEG** (`Vector*.h`): vector LFO family; depends on internal `osc[ch]` time state.
- **Relabi** (`Relabi.h`): time-based phase coupling.
- **Shuffle** (`Shuffle.h`): uses `OC::CORE::ticks` for swing timing.
- **Xfader** (`Xfader.h`): uses `millis()` for balance ramp.

The audit re-reads each vendor header end-to-end to confirm fit against the cat-C boundary. Some may carry external dependencies (osc class, lorenz state class) that defer them further.

## Out of scope (defer to Phase 6+)

- Category-D stateful sequencers.
- CVInputMap shim port + Combin8.
- MIDIQuantizer / scales port + cat-E/F bucket.
- MIDI / audio DSP / system-applets.
- Pair-applet variants.

## Vendor pin

Record the current pinned SHA of `vendor/O_C-Phazerville` at the top of the brainstorm. Phase 4 pinned at `7800d929`. Re-audit applet inventory if changed.

## Existing ports are the primary reference

Read these files in full before drafting anything:

- `applets/Hemispheres.cpp`. Registration macro + test seam.
- `shim/include/applet_indices.h`. Enum (now 31 real applets + Empty).
- `shim/include/HemispheresFactory.h`. Registration table.
- `shim/include/PhzIcons.h`. Icon externs.
- `shim/include/HSicons.h`. Shared icon externs.
- `shim/include/HSUtils.h`. Hemisphere utilities + macros.
- `shim/include/HemisphereApplet.h`. Base class.
- `shim/include/Arduino.h`. Arduino stubs (including the Phase 4 `micros()` shim).
- `shim/include/hemispheres_shim.h`. The step() loop where `ticks_this_step = numFrames / 3 = 10` lives; this is the load-bearing file for the time-injection helper.
- `shim/include/OC_core.h`. `OC::CORE::ticks` declaration.
- `shim/include/util/util_math.h`. Free-function `Proportion` (Phase 4 addition).
- `harness/tests/applet_test_helpers.{h,cpp}`. Pack helpers + `step_n_frames`.
- `harness/tests/test_hemispheres.cpp`. All existing TEST_CASEs.

## Preflight report

After audit, post a preflight report with:

- Pinned `vendor/O_C-Phazerville` SHA (confirm `7800d929` or state new SHA).
- `~/.claude/rules/parallel-execution.md` status.
- Already-ported applet count (read from factory table).
- Phase 4 shim additions reverified: `LOOP_ICON`, `X_NOTE_ICON`, `Changed()`, `DrawSlider`, `randomSeed`, `micros`, 7 Phase-4 icons, free-function `Proportion`.
- Time-injection helper design choice (new helper API vs ticks_this_step override; pick one with justification).
- Per-applet audit for the 7-9 cat-C candidates: state which fit cleanly, which carry deferred deps, which need additional shim work.
- Test baseline: 155 cases / 1803 assertions at `c68d6bc`.
- Confirm `make test-applets` + `make arm` pass on `main` at branch creation.

## Brainstorm requirements

- Vendor pin at top.
- Time-injection helper design + alternative + tradeoffs.
- Cat-C operational boundary (predicates + fit + borderline examples).
- For each candidate, state Status (in scope; depends on shim prereq X; deferred to Phase 6).
- ResetClock disposition + spec rewrite plan citing the 5 facts.
- Selection criteria + scoring table for the 5-8 cat-C selection.
- Phase 6 carry-forward inventory.

## Spec requirements

- Inherit Phase 4 recipe section + section markers convention.
- New "Time-injection helper" subsection. Document the helper API, semantics (how it interacts with 10x rule + countdowns), test usage pattern (when to use vs `step_n_frames`).
- Per-applet entries follow Phase 4 shape: vendor file + lines, category (C), Status, OnDataRequest layout with `file:line`, Start defaults, Controller observables, test concerns (state which coverage shape; cat-C applets must state which inner-tick budget their tests need), shim prereqs, closest analogue.
- ResetClock entry cites all 5 facts from the abort report.
- Spec footer: Recipe spot-check (3 claims) + Per-entry verification (3 entries; if more than 1 of 3 wrong, audit every entry) + Shim prereq verification (every Layer 0 addition).

## Plan requirements

- Start with Dependency declaration citing `~/.claude/rules/parallel-execution.md`.
- Worktree-dispatch checklist invocation as first step.
- Pre-commit hook with `phase5-port/*` + `phase5-shim/*` patterns + `dr/phase5-applets-plan` accept-list.
- DAG structure: Layer 0 helper + Layer 0 shim prereqs / Layer 0.5 section markers / Layer 1 applet ports (parallel by default; document if collapsing inline) / Layer 2 integration / Layer 3 verification.
- Helper verification: Layer 0 commit includes a test exercising the helper against an existing applet (Cumulus is a good probe).
- Plan length under 500 lines.
- Spec coverage check table at the end.

## Hard constraints

- Plan declares parallel vs sequenced tasks explicitly.
- Independent applets do not appear as paste-verbatim task blocks.
- Section markers in test files isolate each implementer's surface.
- Verify OnDataRequest bit layout, Start defaults, Controller semantics against vendor with `file:line` citations.
- 10x clocked-multiplier rule still applies; helper does NOT bypass it.
- Time-injection helper has a unit test before any cat-C applet test uses it.

## Lessons inherited from Phase 4

Apply these without re-deriving. CLAUDE.md at HEAD encodes them:

- Vendor compat headers usually compile against shim as-is (HSProbLoopLinker, SegmentDisplay precedent). Check before stubbing.
- `Proportion` is a free function in `util/util_math.h`. Don't shadow it as a member.
- Submodule init per new worktree: `git submodule update --init --recursive --depth=1`.
- Section markers + inline parent-agent workflow can match parallel-subagent wallclock; if you collapse Layer 1+2 inline, document it as load-bearing.
- Single-shot `set_gate` tests need `clear_bus` between steps.
- IOFrame `arr[4] = { -1 }` only initializes first element.

## Output paths

- Brainstorm: `docs/superpowers/brainstorms/YYYY-MM-DD-phase5-applets-brainstorm.md`.
- Spec: `docs/superpowers/specs/YYYY-MM-DD-phase5-applets-design.md`.
- Plan: `docs/superpowers/plans/YYYY-MM-DD-phase5-applets-plan.md`.

Use the date the document is first written.

## Abort budget

Surface immediately to the user + halt if:

**During audit (before preflight):**

- `~/.claude/rules/parallel-execution.md` missing worktree-dispatch checklist.
- Vendor SHA changed and applets moved.
- `make test-applets` or `make arm` fails on `main` at branch creation.

**During planning:**

- Time-injection helper design has no clean path forward (e.g., the 10x multiplier loop can't be cleanly factored).
- More than 3 of the 7-9 cat-C candidates carry deferred deps reducing scope below 5 applets.
- Per-entry verification reveals more than 1 of 3 entries contradicts vendor reality AND full audit finds more than 5 additional defects.
- Plan exceeds 500 lines.

**During Layer 0:**

- Helper unit test fails against the probe applet.
- Helper breaks any existing test in `make test-applets`.
- `make arm` fails or produces warnings.

**During Layer 1:**

- More than 2 implementer subagents abort substantively.
- Any implementer commits files outside the allowed surface despite the hook.
- Fan-out wall clock exceeds 5 hours.

**During Layer 2:**

- `make test-applets` regresses any existing test.
- `make arm` fails or warns.

**During Layer 3:**

- Hardware smoke check reveals behavior mismatch with host tests.

## Reporting

End-of-run message:

1. Documents produced (paths).
2. Phase 5 scope: cat-C in-scope count, shim/helper additions, deferred-to-Phase-6 count.
3. Layer 0 additions landed with commit SHAs.
4. Layer 1 result: success count, abort count with reasons, wall-clock.
5. Layer 2 integration: pass/fail.
6. Layer 3 final verification: host tests, `make arm`, hardware smoke check.
7. PR URL.

If an abort fired, post the abort report instead.

## Success criteria

- Three documents exist at declared paths.
- Time-injection helper landed in Layer 0 with unit test green.
- ResetClock entry cites all 5 facts from abort report.
- Every in-scope cat-C applet has a spec entry with vendor citations.
- Spec footer has Recipe spot-check + Per-entry verification + Shim prereq verification.
- Plan structures DAG with parallel default + Layer 0 helper + Layer 0.5 markers + Layer 1 ports + Layer 2 integration + Layer 3 verification.
- Plan under 500 lines.
- `make test-applets` passes with no regressions against the Phase 5 branch creation baseline.
- `make arm` clean, zero warnings.
- Hardware smoke check passes for 3 in-scope applets (ResetClock + 2 cat-C).
- PR open on `dr/phase5-applets-plan` with Load-bearing decisions summary.

## First actions

1. Create worktree: `git worktree add .worktrees/phase5-applets-plan -b dr/phase5-applets-plan main`. cd into it. Initialize submodules.
2. Run `make test-applets` + `make arm` to confirm baseline green. Record case + assertion counts.
3. Read the primary references (especially `shim/include/hemispheres_shim.h` for the time-injection design).
4. Read Phase 4 brainstorm/spec/plan + the ResetClock abort report.
5. Verify `~/.claude/rules/parallel-execution.md` worktree-dispatch checklist is present.
6. Audit vendor `applets/` for cat-C candidates. Apply boundary predicates. Score against criteria.
7. Design the time-injection helper. Pick API (new helper vs ticks_this_step override). Document tradeoffs.
8. Post preflight report. Proceed to brainstorm.
9. Write brainstorm, spec, plan.
10. Execute Layer 0 (helper + cat-C shim prereqs).
11. Execute Layer 0.5 (section markers).
12. Execute Layer 1 (applet ports).
13. Execute Layer 2 (integration).
14. Execute Layer 3 (verification).
15. Open PR.
16. Post final report.

If an abort condition fires, halt + surface immediately.
