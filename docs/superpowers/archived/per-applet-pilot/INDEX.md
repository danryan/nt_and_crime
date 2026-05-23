# Per-applet pilot release (archived)

Pilot port of 6 applets to the per-applet plug-in model introduced by the `2026-05-19-per-applet-refactor-kickoff.md` prompt. Established the per-applet runtime header (`plugins/applets/_per_applet_runtime.h`), the applet manifest layout (`shim/include/applet_manifests/*.h`), and the host plug-in scaffolding pattern.

The kickoff prompt (`docs/superpowers/prompts/2026-05-19-per-applet-refactor-kickoff.md`) stays under `prompts/`; do not move it.

## Files

- [per-applet-pilot-brainstorm.md](2026-05-19-per-applet-pilot-brainstorm.md): brainstorm.
- [per-applet-pilot-design.md](2026-05-19-per-applet-pilot-design.md): spec.
- [per-applet-pilot-plan.md](2026-05-19-per-applet-pilot-plan.md): plan.

## Status

Shipped via PR #12. Pilot scope: Compare, ClockDivider, VectorLFO, Cumulus, Relabi, ProbabilityDivider.
