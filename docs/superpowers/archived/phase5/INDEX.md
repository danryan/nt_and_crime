# Phase 5 (archived)

Phase 5 pivoted at preflight from category-C applet ports to a vendor dep port batch. Shipped via merge `4df1c6a`. Bundled VectorOscillator + WaveformManager + RelabiManager, Lorenz, tideslite + PhaseExtractor, full ClockManager, Quantizer subsystem, CVInputMap, plus the time-injection helper.

The Phase 5 kickoff prompt (`docs/superpowers/prompts/2026-05-18-phase5-deps-kickoff.md`) stays under `prompts/` per the CLAUDE.md reference; do not move it.

## Files

- [phase5-deps-brainstorm.md](2026-05-18-phase5-deps-brainstorm.md): brainstorm.
- [phase5-deps-design.md](2026-05-18-phase5-deps-design.md): spec.
- [phase5-deps-plan.md](2026-05-18-phase5-deps-plan.md): plan.

## Status

Shipped via merge `4df1c6a`. The vendor dep ports remain load-bearing for several per-applet plug-ins (VectorMorph, VectorLFO, LowerRenz, all quantizer-using applets).
