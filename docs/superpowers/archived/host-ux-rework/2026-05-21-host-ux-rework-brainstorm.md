# Brainstorm: per-applet host UX rework

Date: 2026-05-21
Owner: Dan
Predecessor: Mass-port release (PR #14, squash `4e8cfd2`) shipped 49 per-applet plug-ins plus two composer hosts (Hemispheres, Quadrants). This release inherits Layer 0.4 deferral from that release.
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`. Verified in worktree.

## Scope

Replace the raw-slot-index host UI on `plugins/hosts/Hemispheres_host.cpp` and `plugins/hosts/Quadrants_host.cpp` with two coupled changes:

1. Enum slot selector that scrolls through Hemi-prefix algorithms in the current preset by name, not raw slot index.
2. Parameter proxying that aggregates each watched slot's `_NT_parameter[]` table onto the host's parameter page, so the user adjusts per-applet bus mappings and applet-specific params from the host's algo view without navigating away.

These two changes are tightly coupled by the host parameter table layout but independent of any applet `.cpp`. Per-applet plug-ins are immutable for this release.

## Why this is its own release

Deferred from mass-port because Task 1 (reentrancy of `NT_setParameterFromUi` called from `parameterChanged`) is hardware-gated and load-bearing. The whole proxy design hinges on the outcome. Shipping mass-port without it kept that release's hardware smoke check small and known-good.

## Vendor ABI surface (firmware contract, no shim work needed)

- `NT_algorithmCount()` returns preset slot count.
- `NT_getSlot(slot, idx)` populates `_NT_slot` for `idx`. `_NT_slot` exposes `name()`, `guid()`, `plugin()`, `numParameters()`, `parameterInfo(info, p)`, `parameterValue(p)`.
- `NT_setParameterFromUi(slot_idx, slot_param_idx, val)` updates a slot's parameter and may trigger that slot's `parameterChanged`. Documented "safe to call from anywhere"; reentrancy from inside host's own `parameterChanged` unverified.
- `NT_updateParameterDefinition(self_slot, param_idx)` tells firmware the parameter struct at that index has changed (e.g. enum strings, min, max). Confirmed legal from `parameterChanged`.
- `parameterChanged(self, p)` is the optional `_NT_factory` hook the host does not currently set. Adding it is part of Task 3.

## Task categorization

| Task | Type | Hardware? | Depends on |
|------|------|-----------|------------|
| 1. Reentrancy probe | diagnostic plug-in | yes | none |
| 2. Enum slot selector | host plug-in change | no for unit, yes for smoke | Task 1 result if proxy share state |
| 3. Parameter proxying | host plug-in change | no for unit, yes for smoke | Task 1 result + Task 2 enum index |
| 4. Per-host integration | wiring | yes for smoke | Task 2 + 3 |

Tasks 2 and 3 share the host parameter table layout. They must be implemented together for a given host, not split. The natural unit of work is a single host (Hemispheres or Quadrants) carrying both Task 2 and Task 3.

The two hosts can ship in parallel once the shared proxy aggregator helper is stable. Helper lifts to `shim/include/host_helpers.h` plus a new `shim/include/host_proxy.h`.

## Per-host status

| Host | Slots | kNumSlotIndexParams | kMaxProxyParamsPerSlot | Total params | Status |
|------|-------|---------------------|------------------------|--------------|--------|
| Hemispheres | 2 | 2 | 16 | 2 + 16*2 = 34 | not started |
| Quadrants | 4 | 4 | 16 | 4 + 16*4 = 68 | not started |

Per-applet `_NT_parameter[]` counts vary; the largest applet (Quantizer, Lorenz) carries ~10-12 params. 16 is a safe budget; can be reduced to 12 if firmware host-param cap matters. Confirm cap during Task 1 hardware visit.

## Risks and abort criteria

- **Reentrancy is synchronous and uncontrolled.** Probe outcome forces the proxy design to add a re-entry guard or deferred queue. Halt and post abort report under `docs/superpowers/abort-reports/` if firmware exposes no safe forward path.
- **Quadrants total param count exceeds firmware host-param cap.** Halt and design fixed-page proxy slot or drop `kMaxProxyParamsPerSlot` to 12 or 8.
- **`NT_updateParameterDefinition` does not propagate enum-string changes mid-session.** Halt; redesign Task 2 to use a fixed-size enum populated once at `construct`, refreshed only across `construct` boundaries (loses dynamic-rescan but is implementable).

## Exclusions

- No applet `.cpp` changes.
- No Y-offset shim work (`HS::gfx_offset_y` already landed in mass-port Layer 0.5).
- No `HemiPluginInterface` ABI changes (proxying uses firmware ABI, not the per-applet interface struct).
- No new probes beyond `reentrancy_probe.cpp`.

## Phased order

1. Probe build (worktree, no hardware).
2. Probe deploy + observe (hardware, Dan).
3. Reentrancy result documented in `docs/superpowers/prompts/2026-05-20-host-ux-rework-kickoff.md` "Reentrancy result" section.
4. Shared proxy aggregator helper in `shim/include/host_proxy.h`.
5. Hemispheres host carries Task 2 + Task 3 via helper.
6. Quadrants host carries Task 2 + Task 3 via helper (parallel with step 5 once helper is stable).
7. Host-level unit tests in `harness/tests/test_host_proxy.cpp`.
8. Hardware smoke (Dan).
9. PR open against `main`.
