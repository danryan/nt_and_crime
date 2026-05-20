# Per-applet host UX rework (deferred from mass-port)

Date: 2026-05-20
Owner: Dan
Predecessor: Mass-port release at `docs/superpowers/prompts/2026-05-20-per-applet-mass-port-kickoff.md` ships first; this release follows.
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`. Re-verify at kickoff.

## Scope

Replace the raw-slot-index host UI on `plugins/hosts/Hemispheres_host.cpp` and `plugins/hosts/Quadrants_host.cpp` with an enum slot selector and parameter proxying so users can adjust per-applet bus mappings and applet-specific params from the host's algo view without navigating away.

This work was originally Layer 0.4 of the mass-port. It deferred because it is independent of any specific applet port and because one sub-task (verifying `NT_setParameterFromUi` reentrancy from `parameterChanged`) is load-bearing and not yet investigated.

## Task 1: reentrancy investigation (gates the rest)

`NT_setParameterFromUi(slot_idx, slot_param_idx, value)` called from inside the host's `parameterChanged(host_param_idx)` could re-enter the host's own `parameterChanged` if firmware notifies all listeners synchronously. The proxy design assumes a one-shot forward; if firmware loops, proxies may double-fire or recurse.

Verify on hardware before any host coding:

1. Write a minimal probe plug-in with two parameters. On `parameterChanged(0)`, call `NT_setParameterFromUi(self_slot, 1, new_value)`. Observe whether `parameterChanged(1)` fires on the same call stack or on a later step.
2. Deploy via `make deploy-sysex`. Add probe to a preset slot. Use `mcp__nt_helper__show_screen` after each parameter edit to read state.
3. Document outcome in this kickoff's "Reentrancy result" section below.

Halt the release if reentrancy is synchronous and uncontrolled. The proxy design must add a re-entry guard or route forwards through a deferred queue.

## Task 2: enum slot selector

Replace `Slot N index` uint8 param with enum scrolling Hemi-prefix algorithms in the preset.

1. At each `draw()`, scan `NT_algorithmCount()` slots and build a list of `{slot_index, name}` for any algorithm with guid prefix `'Hm'`.
2. Use `NT_updateParameterDefinition` to refresh the parameter's enum strings to that list.
3. The parameter value is now an enum index into that list, not a raw slot index.
4. `resolve_slot` is called with the actual `slot_index` resolved from the enum index.

## Task 3: parameter proxying

Aggregate each watched slot's `_NT_parameter[]` table into the host's parameter page.

1. Host `calculateRequirements`: budget `numParameters = kNumSlotIndexParams + kMaxProxyParamsPerSlot * kNumSlots` (Hemispheres: `2 + 16*2 = 34`; Quadrants: `4 + 16*4 = 68`).
2. Host `construct`: after slot indices known, call `NT_getSlot(slot, watched_idx)` for each watched slot, iterate `slot.numParameters()`, copy via `slot.parameterInfo(info, p)` into the host's parameter table starting at offset `kNumSlotIndexParams`. Prefix each proxy name with `"S0:"` / `"S1:"`.
3. Host `parameterChanged(host_param_idx)`: if proxy, decode `(slot_idx, slot_param_idx)` mapping; call `NT_setParameterFromUi(slot_idx, slot_param_idx, host->v[host_param_idx])`. Honor reentrancy result from Task 1.
4. Slot-index changes trigger `NT_updateParameterDefinition` refresh so proxy params re-resolve to the new slot's params.

## Out of scope

- Any applet `.cpp` change. Per-applet plug-ins are immutable for this release.
- Y-offset shim (`HS::gfx_offset_y`). Landed in mass-port Layer 0.5.
- ABI changes to `HemiPluginInterface`. Proxying uses firmware `NT_getSlot` / `NT_setParameterFromUi` only.

## Reentrancy result

(Populated after Task 1.)

## Abort conditions

- Task 1 returns synchronous uncontrolled reentrancy with no firmware mitigation path; halt and post abort report.
- Total proxy-aggregated parameter count for Quadrants exceeds firmware host parameter cap; halt and document constraint.
- `NT_updateParameterDefinition` does not propagate enum-string changes mid-session; halt; design must fall back to fixed-size enum populated at construct time.
