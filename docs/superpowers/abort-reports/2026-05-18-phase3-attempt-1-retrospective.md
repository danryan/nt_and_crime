# Phase 3 attempt-1 retrospective

Date: 2026-05-18
Status: Aborted. Re-attempt pending spec audit + corrected dispatch.

## Outcome

Dispatched 15 parallel implementer subagents. Returns: 6 SUCCESS clean, 4 SUCCESS dirty (contract violation), 5 ABORTED. Net salvageable: 0 commits landed; all 10 implementer commits archived under `refs/archive/phase3-attempt-1/<applet>` for audit reference.

Branch topology after abort: only `main` and `dr/phase3-applets-plan` remain. 10 agent worktrees removed. 12 implementer-related branches deleted (refs preserved via archive).

## Findings (5 categories)

1. **Spec defects, vendor-semantics misread**. Spec author over-relied on `OnDataRequest` layout + `Start()` body inspection without tracing full `Controller()` logic. Surfaced via implementer aborts:
   - **ResetClock**: spec claims `Clock(0)` increments `position` and Reset fires when `position == offset`. Vendor (`ResetClock.h:46-71`): `position` only increments inside the output block; Reset (`ClockOut(1)`) fires when `pending_clocks == 1` during the output cycle. `Clock(1)` is the actual external Reset input. `Start()` doesn't reset `offset_mod`, so first Controller tick after `OnDataReceive` runs `UpdateOffset` and jumps `pending_clocks` by the offset value. Spec entry: full rewrite. Demote to Phase 4. Detailed report: `docs/superpowers/abort-reports/2026-05-18-resetclock-spec-mismatch.md`.
   - **Switch**: spec describes `active[]` semantics with values {0, 1}. Vendor sets `active[0] = step + 1`, values {1, 2}. Plus spec SW2 case (drive Clock(0) once, assert step toggle visible at bus) is defeated by the 10x shim multiplier (10 toggles per buffer = even = invisible). Implementer worked around by driving channel-1 gated switch instead. Spec entry needs the corrected active[] semantics + SW2 redesign.

2. **Pre-existing-deferral miss**. Spec ignored prior project deferrals.
   - **Binary**: previously deferred per `docs/shim-additions.md:214,241` pending SegmentDisplay shim port. Spec entry treated as in-scope. Plus depends on `HEMISPHERE_3V_CV` macro and `PhzIcons::binaryCounter` icon, neither present in shim. Three independent shim-port prereqs. Demote to Phase 4 pending SegmentDisplay.

3. **Shim 10x-multiplier interaction with applet-internal state**. Recipe defect, not per-entry.
   - **ProbabilityDivider**: vendor Controller has internal `skip_steps` counter inside `if (Clock(0))`. Shim's `clocked[0]` flag asserted for all 10 inner Controller ticks per buffer. With `weight_8=15` (only), the 10 internal ticks fire `ClockOut(0)` twice per Clock(0) buffer instead of once-per-8-edges. `clock_countdown` saturates at `HEMISPHERE_CLOCK_TICKS=175` ticks, so output stays high for ~17 buffers between fires. The "1 fire per N input clocks" assertion is unreachable for any test driving pattern. Spec needs round-trip + state-injection coverage only; no bus-level fire-count assertions.
   - **ClockDivider** (success-with-finding): CD2/CD3 cannot distinguish `div=2` from `div=4` at the bus boundary. Same root cause. Spec described tests as if shim ran one Controller call per Clock edge.
   - **PolyDiv** (success-dirty-with-finding): same. PDV2/PDV3 weakened to routing + disable-branch verification.
   - **Stairs** (success): handled correctly because spec entry explicitly called out the 10x caveat with Cumulus CU2 precedent. Implementer modeled the multiplier in ST2/ST3 assertion math.
   - **RunglBook** (success): 10x folded into RB2's expected reg state (shift 10 ones in, low 3 bits = 7, Out = MAX_CV).
   - **Switch** (above): SW2 redesigned.
   - Pattern: every applet whose Controller has any internal accumulator, counter, or toggle inside `if (Clock(ch))` is affected. Recipe section needs an explicit rule. Bus-level behavioral assertions for these applets must either model the multiplier (Stairs, RunglBook, Cumulus precedent) or move coverage to round-trip + state-injection (ProbabilityDivider).

4. **Missing shim infrastructure not catalogued per applet**. Plan Step 5 assumed one-line workaround (`applet_indices.h` enum value). Real per-applet cost: 4-7 edits (`applet_indices.h` enum + `HemispheresFactory.h` `#include` + `applet_enum_strings[]` + `kMaxAppletSize` `cmax` + `kMaxAppletAlign` `cmax` + `applet_factory()` table + 1-3 icon byte arrays in `PhzIcons.h` + `icons.cpp`). Some applets also need shim helpers that don't yet exist: `HEMISPHERE_3V_CV` macro (Binary, ShiftGate), `Changed(int ch)` HemisphereApplet method (Trending), `ForAllChannels` 4-iteration form (PolyDiv), `HEMISPHERE_PULSE_ANIMATION_TIME` constant (PolyDiv), `CHECK_ON_ICON`/`CHECK_OFF_ICON` (PolyDiv), `STAIRS_ICON`/`UP_BTN_ICON`/`RESET_ICON` (Stairs), `LOOP_ICON`/`X_NOTE_ICON` (ShiftGate). Per-applet shim-prereq audit must precede any re-dispatch.
   - **Trending**: missing shim `Changed(int ch)` member function. Demote to Phase 4 pending shim port.

5. **Process defects: worktree dispatch + Step 7 guard.**
   - **Worktree base branch**: all 10 implementer worktrees branched from `main` instead of `dr/phase3-applets-plan`. Phase 3 spec/plan/brainstorm absent from worktree at expected path. 6 implementers self-recovered (via Phase 2 plan recap, branch checkout, recipe inference); the other 5 either aborted on the spec-path issue or surfaced substantive spec defects after finding the spec via alternate path. Root cause: parent agent did not specify base branch in `git worktree add`. Default was `main`. Durable fix landed in `~/.claude/rules/parallel-execution.md` (worktree-dispatch checklist).
   - **Step 7 guard ineffective**: 4 of 10 successful implementers committed shim edits in violation of the plan's "do not commit shim/" rule. Prose guard ("verify `git diff --cached` shows no changes to shim/include/") was not enforced. Future plans need a pre-commit hook in the worktree that fails the commit if forbidden files are staged. Prose alone does not survive subagent contract-bending.
   - **Per-entry spot-check missing**: framework had a recipe-level spot-check but no per-applet spot-check. Recipe was correct; per-applet derivations from the recipe were not all correct. Five substantive aborts surfaced spec defects that a per-entry spot-check would have caught before fan-out. New framework rule: when authoring a spec that generalizes a pattern across N instances, spot-check a random sample of N/5 (minimum 3) per-applet entries by walking Controller() and Start() end-to-end against the entry's observables and Test concerns. Record the spot-check results in a footer.

## Tally

| Applet | Status | Disposition |
| --- | --- | --- |
| ClockSkip | SUCCESS clean | Audit + replay |
| Schmitt | SUCCESS clean | Audit + replay |
| RndWalk | SUCCESS clean | Audit + replay |
| ClockDivider | SUCCESS clean | Audit + replay; recipe rule needed |
| Stairs | SUCCESS clean | Audit + replay (precedent for 10x handling) |
| Switch | SUCCESS clean | Audit; spec entry needs active[] correction |
| RunglBook | SUCCESS dirty | Replay clean; shim wiring rolled into integration |
| Voltage | SUCCESS dirty | Replay clean |
| ShiftGate | SUCCESS dirty | Replay clean; HEMISPHERE_3V_CV shim addition needed |
| PolyDiv | SUCCESS dirty + spec defect | Spec rewrite (4 dividers, not 2); replay |
| EnvFollow | ABORTED (logistics) | Spec audit; expected to pass replay after path fix |
| ResetClock | ABORTED (spec defect) | Demote to Phase 4 |
| Binary | ABORTED (spec defect + prior deferral) | Demote to Phase 4 pending SegmentDisplay |
| ProbabilityDivider | ABORTED (spec defect, multiplier) | Spec rewrite (round-trip + state-injection only); replay |
| Trending | ABORTED (missing shim member) | Demote to Phase 4 pending shim port |

Anticipated Phase 3 retry set after audit: 11 applets (15 minus ResetClock, Binary, Trending; plus PolyDiv spec rewrite, ProbabilityDivider spec rewrite). Final number after audit may shrink further.

## Archive references

```
refs/archive/phase3-attempt-1/clock_divider     3e8d649
refs/archive/phase3-attempt-1/clock_skip        4f5aac7
refs/archive/phase3-attempt-1/poly_div          a02d991
refs/archive/phase3-attempt-1/rnd_walk          312db63
refs/archive/phase3-attempt-1/rungl_book        514bc08
refs/archive/phase3-attempt-1/schmitt           6c51922
refs/archive/phase3-attempt-1/shift_gate        dda90c8
refs/archive/phase3-attempt-1/stairs            e412ab0
refs/archive/phase3-attempt-1/switch            2c5c0d3
refs/archive/phase3-attempt-1/voltage           c2af5c8
```

`git show refs/archive/phase3-attempt-1/<applet>` retrieves the implementer's commit; `git diff main..refs/archive/phase3-attempt-1/<applet>` shows the test body for audit reference.

## Next steps

1. Spec audit pass. Walk each in-scope applet's `Controller()` and `Start()` against the existing spec entry. Catalog per-applet shim-prereqs. Demote applets the audit invalidates.
2. Plan revision. Step 5 acknowledges 4-7 shim edits per applet (or a pre-integration shim-prep commit). Step 7 guard becomes a pre-commit hook in the worktree. Recipe section gets the 10x-multiplier rule.
3. Dispatch revision. `~/.claude/rules/parallel-execution.md` worktree-dispatch checklist already landed; parent agent must follow it for retry.
4. Retry on the audited subset. Likely 10-12 applets, not 15.

## What this attempt produced (audit inputs)

- Confirmed list of spec defects: 5 substantive aborts, plus Switch and ClockDivider findings on the 10x multiplier.
- Confirmed recipe defect: bus-level assertions under the 10x multiplier.
- Confirmed plan defect: Step 5 understated shim-prereq cost by ~6x.
- Confirmed process defect: worktree dispatch was not isolated from `main`.
- Confirmed framework gap: per-entry spot-check was not required by any prior rule.

These five categories of finding came out of one attempt and would not have surfaced without dispatching the work. The cost was a contaminated topology and wasted compute on the dirty/aborted runs. The return is the audit inputs above, plus the durable rule landed in `~/.claude/rules/parallel-execution.md`.
