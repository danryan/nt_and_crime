# Abort report: ResetClock spec mismatch

Date: 2026-05-18
Phase: 3 (parallel implementer fan-out)
Applet: ResetClock
Status: ABORTED at implementer level. Demoted from Phase 3 to Phase 4.

## Context

Phase 3 implementer for ResetClock invoked the abort rule (defined in `docs/superpowers/plans/2026-05-18-phase3-applets-plan.md`, Implementer task shape) after discovering the spec entry contradicts vendor Controller behavior in load-bearing ways. No commit produced. This report captures the implementer's findings verbatim for the Phase 4 author.

## Vendor source cited

- `vendor/O_C-Phazerville/software/src/applets/ResetClock.h:46-71` (Controller body).
- `vendor/O_C-Phazerville/software/src/applets/ResetClock.h:102-107` (`UpdateOffset`).
- `vendor/O_C-Phazerville/software/src/applets/ResetClock.h:32-40` (`Start()`).
- `shim/include/HSUtils.h:11` (`HEMISPHERE_CLOCK_TICKS = 175`, vendor uses `RC_TICKS_PER_MS = 175`).
- `shim/include/hemispheres_shim.h:156-168` (`ticks_this_step = numFrames / 3 = 10`).

## Spec defects identified

1. Spec line 175 says "Clock(0) increments `position`; when `position == offset` the applet fires a Reset (ClockOut(1))." Vendor: `position` only increments inside the output block (`ResetClock.h:66`), not on Clock(0). Reset (ClockOut(1)) fires solely when `pending_clocks == 1` during the output cycle (`ResetClock.h:62-64`). `position` does not gate Out(1).

2. Spec RC2 case "length=4, offset=0; drive 4 Clock(0) edges and assert Reset fires on the first." Vendor: a single Clock(0) edge sets `pending_clocks=1`. After `spacing * 175` ticks, both ClockOut(0) and ClockOut(1) fire on the same output cycle (because `pending_clocks == 1`). It is not the first Clock(0) edge that fires Reset; it is the output cycle ~1050 ticks later when the pending queue holds exactly one entry.

3. Spec RC3 case "length=4, offset=2; assert Reset fires on the third edge." Vendor: with `offset=2`, the first Controller call after `OnDataReceive` enters the `cv1_mod != offset_mod` branch and `UpdateOffset(offset_mod, 2)` runs `pending_clocks += 2 - 0 = 2`. So `pending_clocks == 2` immediately, with zero Clock(0) input. The pending queue drains over two output cycles; Reset fires on the second output cycle. Adding three Clock(0) edges pushes `pending_clocks` to 5 and delays Reset to the fifth output cycle. There is no Clock-(0)-counting interpretation that makes "third edge" true.

4. `Start()` (`ResetClock.h:32-40`) does not reset `offset_mod`. Any test that builds on a "fresh state" assumption breaks on the first-tick `UpdateOffset` jump described in (3).

5. Spec analogue "ClkToGate" is wrong. ClkToGate is gate-shaping with per-side independent timing. ResetClock has cross-input dependency (offset modulation, pending queue, position cycling). Closer analogues: Burst (pending pulse queue, spacing time, `RC_TICKS_PER_MS`-style timing) or Cumulus (multi-field with bias).

## Implications for Phase 4 rewrite

The Phase 4 author must:

- Reframe the observables in terms of `pending_clocks` evolution over output cycles, not Clock(0) edge counts.
- Recognise Clock(1) as the external Reset input (the only place vendor `Reset()` is called).
- Cover the `offset_mod` first-tick jump as its own test concern (initial state is non-trivial).
- Re-anchor the analogue on Burst.
- Re-evaluate the category. `RC_TICKS_PER_MS = 175` plus `ticks_this_step = 10` means one spacing step is `spacing * 175 / 10` buffer steps. With `spacing=6` default that is 105 step() calls per output cycle. ResetClock may need category C (time-sensitive) classification with `spacing=1` test override to stay within budget.

## Phase 3 disposition

ResetClock demoted to Phase 4 mid-execution. Phase 3 in-scope set drops from 15 to 14. The Phase 3 brainstorm, spec, and plan are patched to remove the entry and link this report. No code or commits produced on this branch from the ResetClock implementer.
