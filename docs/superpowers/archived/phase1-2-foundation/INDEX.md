# Phase 1+2 foundation (archived)

Pre-shipping foundational plans plus the original shim design that bootstrapped the project. Phase 1 plus Phase 2 together ported the first 14 applets and established the harness, shim layer, and applet selector pattern that all subsequent phases inherited.

Merges visible in `git log --oneline --merges`:

- `eb7e75c` Merge Plan B/C/D/E pair shim work
- `e44d276` Merge Plan F: Hemispheres runtime applet selector
- `526e590` Merge Plan G: Tier 2 applet expansion

## Files

- [nt-hemisphere-shim-design.md](2026-05-16-nt-hemisphere-shim-design.md): original shim design.
- [plan-a-harness-gate.md](2026-05-16-plan-a-harness-gate.md): harness gate plan.
- [plan-b-shim-and-logic.md](2026-05-16-plan-b-shim-and-logic.md): shim plus Logic applet plan.
- [plan-c-tier1-applets.md](2026-05-17-plan-c-tier1-applets.md): tier 1 applet ports.
- [plan-d-bitmap-icon-fidelity.md](2026-05-17-plan-d-bitmap-icon-fidelity.md): bitmap icon parity.
- [plan-e-multi-applet-pair.md](2026-05-17-plan-e-multi-applet-pair.md): multi-applet pair host shape.
- [plan-f-hemispheres-runtime-applet-selector.md](2026-05-17-plan-f-hemispheres-runtime-applet-selector.md): runtime applet selector inside the bundled host.
- [plan-g-tier2-applet-expansion.md](2026-05-17-plan-g-tier2-applet-expansion.md): tier 2 applet expansion.

## Status

Shipped. Superseded by the per-applet plug-in model (per-applet pilot release plus mass-port release). Material here is preserved for audit trail; the actual surfaces these docs describe are mostly retired or transformed.
