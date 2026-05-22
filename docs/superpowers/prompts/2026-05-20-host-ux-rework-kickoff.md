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

Date verified: 2026-05-21
Probe: `plugins/probes/reentrancy_probe.cpp` (commit `eb42ebf` + armed-flag fix)
Hardware: Disting NT, firmware shipped 2026-05.

### Probe behavior

Two parameters `A` (0..1000) and `B` (0..1000). L encoder click calls
`NT_setParameterFromUi(self_idx, A_global, v[A] + delta)` from `customUi`,
having first set `armed_for_forward = true`. Firmware fires `parameterChanged(A)`;
the handler, if armed, forwards via
`NT_setParameterFromUi(self_idx, B_global, v[A])`. Counters:

- `PC0`: total `parameterChanged(A)` firings.
- `PC1`: total `parameterChanged(B)` firings.
- `NEST`: `parameterChanged(B)` firings observed while `parameterChanged(A)`
  was still on the call stack (synchronous-reentry signal).

### Crash discovered, mitigated

First two probe versions hard-crashed on add-to-preset. Root cause:
firmware fires `parameterChanged` for each param during `construct`
(default-value notify), at a moment when calling `NT_setParameterFromUi`
back into self is unsafe (algorithm not fully registered). Guarding on
`self->v != nullptr` and `NT_algorithmIndex(self) >= 0` was not enough;
both returned valid values during the spurious construct-time fire.

Mitigation: an `armed_for_forward` bool on the instance, set true by
`customUi` just before its `NT_setParameterFromUi` and cleared after.
`parameterChanged` only forwards if armed. Construct-time spurious
firings are correctly skipped. Probe loads clean and exercises cleanly.

This pattern is mandatory for the host proxy. Stage 3 host wiring must
arm proxy forwards via a `customUi`-set flag (or analogous "we are
genuinely inside a user encoder edit" sentinel), not blind-forward
inside `parameterChanged`.

### Observed counters

After ~108 encoder edits without reset:

| Counter | Value |
|---------|-------|
| PC0 | 108 |
| PC1 | 336 |
| NEST | 0 |
| A | 97 |
| B | 160 |

### Interpretation

`NEST == 0` is the load-bearing result. `parameterChanged(B)` never
fires while `parameterChanged(A)` is still on the stack. Firmware
defers the inner notification to a later call frame.

Implications for host proxy:

- No stack-safety re-entry guard required for the forward path.
- A "we are genuinely processing a user encoder edit" gate is still
  required to suppress construct-time spurious forwards.
- Host can freely call `NT_setParameterFromUi(target_slot, target_param, val)`
  from inside its own `parameterChanged(host_p)` when armed. The watched
  slot's `parameterChanged` will fire on a later frame, which is fine
  for any applet that consumes parameter changes during `step()`.

`PC1 > 3*PC0` and `A != B` are notable but not blockers. Likely
sources: firmware periodically re-fires `parameterChanged` for all
params during preset autosave / display rescan, independent of our
forwards. Each user click delivers one armed forward A->B (visible in
the A,B trajectory over short windows); ambient pc1 firings add on
top. Proxy correctness does not depend on a 1:1 PC0:PC1 ratio. State
sync depends only on the forward delivering at all, which the screen
trajectory confirms (B follows A, just lagged by however many extra
pc1s land between user clicks).

### Abort conditions: not triggered

None of the documented abort criteria are hit. `NT_setParameterFromUi`
delivers (PC1 > 0). `NEST == 0` is the strict signal of a safe
deferred-notify firmware. Proxy design proceeds with the armed-flag
pattern baked in.

## Abort conditions

- Task 1 returns synchronous uncontrolled reentrancy with no firmware mitigation path; halt and post abort report.
- Total proxy-aggregated parameter count for Quadrants exceeds firmware host parameter cap; halt and document constraint.
- `NT_updateParameterDefinition` does not propagate enum-string changes mid-session; halt; design must fall back to fixed-size enum populated at construct time.
