# Applet tests Track A (archived)

Track-A introduced the per-applet Catch2 host test framework with the seam-based applet access pattern. Phase 1 covered Brancher plus Calculate; Phase 2 covered Logic, AttenuateOffset, Slew, Burst, Compare, GatedVCA, Button, ClkToGate, GateDelay, TLNeuron, Cumulus.

Merges visible in `git log --oneline --merges`:

- `186d617` Merge applet-tests Phase 1: Track A logic tests for Brancher + Calculate.
- `ca2a063` Merge Phase 2: Track A applet tests (11 applets).

## Files

- [design.md](2026-05-17-applet-tests-track-a-design.md): test framework spec.
- [phase1.md](2026-05-17-applet-tests-track-a-phase1.md): Phase 1 plan (2 applets).
- [phase2.md](2026-05-17-applet-tests-track-a-phase2.md): Phase 2 plan (11 applets).
- [phase2-parallel.md](2026-05-17-applet-tests-track-a-phase2-parallel.md): Phase 2 parallel-execution variant.

## Status

Shipped. The test framework continued to drive applet coverage through subsequent phases. The bundled-host test binary (`test_hemispheres`) that this work bootstrapped was retired in the 2026-05-23 cleanup release; per-applet test binaries inherit the coverage shape.
