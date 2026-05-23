# Host UX rework (archived)

Per-applet host UX rework: enum selector plus parameter proxying via the `HemiPluginInterface` slot lookup pattern. Shipped via commit `aa2fa99` (`feat: per-applet host UX rework`).

The kickoff prompt (`docs/superpowers/prompts/2026-05-20-host-ux-rework-kickoff.md`) stays under `prompts/`; do not move it.

## Files

- [host-ux-rework-brainstorm.md](2026-05-21-host-ux-rework-brainstorm.md): brainstorm.
- [host-ux-rework-design.md](2026-05-21-host-ux-rework-design.md): spec.
- [host-ux-rework-plan.md](2026-05-21-host-ux-rework-plan.md): plan.

## Status

Shipped via commit `aa2fa99`. Wired the host_proxy aggregator pattern that `plugins/hosts/Hemispheres_host.cpp` plus `plugins/hosts/Quadrants_host.cpp` rely on for parameter forwarding plus customUi routing.
