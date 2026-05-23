# Per-applet mass-port release (archived)

Mass-port of 49 Hemisphere applets to the per-applet plug-in model. Followed the pilot release that established the pattern.

Merge commit `4e8cfd2` (`feat(plugins): port 49 Hemisphere applets as per-applet plug-ins`).

The kickoff prompt (`docs/superpowers/prompts/2026-05-20-per-applet-mass-port-kickoff.md`) stays under `prompts/`; do not move it.

## Files

- [per-applet-mass-port-brainstorm.md](2026-05-20-per-applet-mass-port-brainstorm.md): brainstorm.
- [per-applet-mass-port-design.md](2026-05-20-per-applet-mass-port-design.md): spec.
- [per-applet-mass-port-plan.md](2026-05-20-per-applet-mass-port-plan.md): plan.

## Status

Shipped via PR #14 (squash `4e8cfd2`). 49 applets now live under `plugins/applets/` plus matching manifests under `shim/include/applet_manifests/`. Combined with the 6 pilot applets, that is 55 per-applet plug-ins.
