# Design: vendor applet UX hardening batch

Date: 2026-05-22
Status: Stage 2 spec, ready for Stage 3 dispatch.
Brainstorm: `docs/superpowers/brainstorms/2026-05-22-applet-ux-hardening-brainstorm.md`
Plan: `docs/superpowers/plans/2026-05-22-applet-ux-hardening-plan.md`
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.

## Context

Three UX quirks observed on hardware after the host UX rework (PR #15) shipped. Discovery sweep (Stage 1) completed 2026-05-22; no quirks beyond Q1-Q3 surfaced after cycling every per-applet plug-in under both Hemispheres and Quadrants hosts.

The three quirks are heterogeneous: a drawing-clip bleed (Q1), a firmware-overlay obscuration (Q2), and a host-table-initialization sloppiness (Q3). Each has an independent fix surface. Stage 3 implementers can run in parallel across all three.

## Canonical recipe

No single recipe spans all three quirks. Each quirk gets its own per-entry recipe below. The Stage-3 dispatch pattern is uniform: one implementer per quirk on a worktree branched off the feature branch, writing failing tests first, with a writable-surface declaration enforced by the pre-commit hook.

## Per-quirk entries

### Q1: Quadrants right-edge bleed (per-applet runtime clip rect)

Mitigation: each per-applet plug-in's `render_view_with_offset` helper sets `HS::gfx_clip_w = 64; HS::gfx_clip_h = 64;` next to the existing `HS::gfx_offset` writes; the shim's `HemisphereApplet::gfx*` helpers clamp emits against the rect. Hosts do not touch the clip globals.

Architectural rationale (corrected from initial spec): every per-applet plug-in TU privately aggregates `shim/src/globals.cpp` via the `_per_applet_runtime.h` → `hem_shim.h` → `hem_shim_impl.h` → `globals.cpp` include chain. Each ARM `.o` therefore has its own copy of every `HS::*` global. A write from the host's TU does NOT reach the per-applet plug-in's gfx helpers (different memory). The clip write MUST live in the same TU that reads it. Host plug-ins are no longer involved.

Affected files:

- `shim/include/HSUtils.h`: declare `extern int gfx_clip_w;` and `extern int gfx_clip_h;` in the `HS` namespace, beside the existing `gfx_offset` / `gfx_offset_y`.
- `shim/src/globals.cpp`: define `HS::gfx_clip_w = 256;` and `HS::gfx_clip_h = 64;` as defaults (full screen, harness-safe).
- `shim/include/HemisphereApplet.h`: clamp emitted pixels in the `gfx*` helpers (`gfxPixel`, `gfxLine`, `gfxFrame`, `gfxRect`, `gfxBitmap`, `gfxPrint`, etc.) against `[HS::gfx_offset, HS::gfx_offset + HS::gfx_clip_w)` x `[HS::gfx_offset_y, HS::gfx_offset_y + HS::gfx_clip_h)`. Pixels with x or y outside the rect are dropped silently. Bounding-box culls go first; per-pixel clamp only for `gfxBitmap` and `gfxPrint`.
- `plugins/applets/_per_applet_runtime.h`: `render_view_with_offset` sets `HS::gfx_clip_w = 64; HS::gfx_clip_h = 64;` right after the offset writes; restores defaults (256 / 64) on exit alongside the offset resets. This is the new integration-step edit (originally listed as forbidden surface; the spec was wrong about the TU layout).
- `harness/tests/test_draw_clip.cpp`: new test file. Cases:
  - default rect (clip_w=256, clip_h=64): a pixel emit at (200, 30) is delivered to the framebuffer.
  - host-set rect via test (clip_w=64, clip_h=64, offset=64): emit at vendor (63, 30) lands at screen (127, 30); emit at vendor (64, 30) is clipped.
  - lower-edge clip: emit at vendor (10, 64) clipped; vendor (10, 63) lands.
  - upper-edge clip: emit at vendor (10, -1) clipped.

Hosts revert to pre-Q1 state: no clip writes, no `HSUtils.h` include.

Expected behavior change: in Quadrants Host with four applets, the rightmost column of each lane no longer bleeds into the neighbor's leftmost column. Hemispheres' 128-px lanes already absorbed the bleed in dead space; behavior there is unchanged at the user-visible level.

Test plan: `make test-applets`, `make test-host-proxy`, `make test-draw-clip`, `make test-buses`, `make test-draw`, `make test-draw-shape`, `make test-hosts-pilot`, `make test-applets-pilot` all green; `make arm` clean; `arm-none-eabi-nm build/arm/Hemispheres_host.o` shows zero unresolved `HS::*` symbols.

### Q2: Encoder-turn footer overlay (claim pot controls; hardware spike)

Mitigation: spike. The NT API up through `kNT_apiVersion13` exposes no overlay-suppress hook. Hosts already claim `kNT_encoderL | kNT_encoderR | kNT_encoderButtonL | kNT_encoderButtonR | kNT_button1 | kNT_button2`. The "push: params, push: snap" footer text references encoder-push (params-page navigation) and pot-button (snap-quantize). One unexplored avenue: hosts do NOT claim any pot controls. Claiming `kNT_potL | kNT_potC | kNT_potR | kNT_potButtonL | kNT_potButtonC | kNT_potButtonR` may suppress the footer the same way claiming an encoder suppresses encoder-handling.

Affected files (writable surface for Q2 implementer):

- `plugins/hosts/Hemispheres_host.cpp::hasCustomUi_impl`: extend return value with the six pot-control bits listed above.
- `plugins/hosts/Hemispheres_host.cpp::customUi_impl`: no-op on pot events (host does not use pots).
- `plugins/hosts/Quadrants_host.cpp::hasCustomUi_impl` and `customUi_impl`: same change.

Forbidden surface for Q2 implementer: anything outside the two host `.cpp` files.

Test plan (spike outcome decides this entry's disposition):

1. Apply the customUi expansion locally.
2. Build `Hemispheres_host.o` and `Quadrants_host.o`.
3. Deploy both via `make deploy-sysex SYSEX_PLUGIN=build/arm/<host>.o SYSEX_ID=0`.
4. Set up a Hemispheres preset with two applets; turn either encoder; observe whether the "push: params / push: snap" footer still appears.
5. Repeat with Quadrants and four applets.
6. Outcome A: footer suppressed. Commit the expansion; add a regression note to CLAUDE.md ("Claiming all pot controls in hasCustomUi suppresses the encoder-turn footer"); ship Q2 as part of this batch.
7. Outcome B: footer unchanged. Revert the expansion. Re-classify Q2 as deferred. Update the brainstorm with the negative result and append a follow-up to the spec footer: "Q2 deferred to firmware feature request; see <issue link>".

Pot-button event handling note: claiming pot buttons may also suppress the snap-quantize default behavior firmware-wide. If a Hemispheres applet relied on pot-button snap, claiming the button without re-implementing snap removes the feature. Confirm on hardware that no applet uses pot-buttons; if any do, restrict the claim to `kNT_potL | kNT_potC | kNT_potR` and re-test. This is the secondary spike.

### Q3: Unused parameter rows show "-OFhW" garbage label

Mitigation: assign a stable placeholder string to the `.name` field of every unbound `_NT_parameter` in the proxy table. Root cause confirmed at `shim/src/host_proxy.cpp:323-326, 345-348, 370-373`: unbound entries currently get `{ nullptr, 0, 0, 0, kNT_unitNone, 0, nullptr }`, with `.name = nullptr`. Firmware reads `inst->parameters[i].name` for every `i < numParameters` and prints uninitialized memory when the pointer is null.

Affected files (writable surface for Q3 implementer):

- `shim/src/host_proxy.cpp`: at the three sites that initialize unused proxy params, set `.name = "--unused--"` (a static string literal; the lifetime is program-static, safe to point at). The accompanying `s.proxy_names[base + p][0] = '\0'` line becomes irrelevant for the unused case but can stay as-is (harmless; firmware reads `.name`, not the proxy_names buffer, for unbound entries).
- `harness/tests/test_host_proxy.cpp` (existing) or a new `harness/tests/test_host_proxy_unused.cpp`: case asserts that after `bind_slot(lane, kInvalidSlotIdx)` or after a slot returns fewer params than `kMaxProxyParamsPerSlot`, every unbound `proxy_params[i].name` points to a string equal to `"--unused--"`. Also cover: after `bind_slot(lane, valid)` followed by `bind_slot(lane, kInvalidSlotIdx)`, all entries from that lane revert to `"--unused--"`.

Forbidden surface for Q3 implementer: host plug-ins (`Hemispheres_host.cpp`, `Quadrants_host.cpp`), applet plug-ins, vendor source, drawing wrappers.

Expected behavior change: parameter page shows `--unused--` for unbound rows instead of the garbage label. Visible on hardware: deploy Hemispheres host with an applet bound to lane 1 that exposes 3 parameters (so 13 of the 16 proxy slots are unbound); enter the parameters page; confirm rows 4-16 read `--unused--`. Repeat for Quadrants with one applet bound.

Test plan: `make test-host-proxy` passes (existing tests + new unused-label tests).

ABI / contract: `--unused--` is a static C string literal in the shim binary; no allocation. No ABI change (the host plug-ins still receive the same `_NT_parameter` struct shape). Memory cost: ~12 bytes of `.rodata`.

## Stage-3 dispatch shape

Three independent implementers in a single message. Each runs in its own worktree branched from this batch's feature branch (not from `main`). Writable surfaces declared above; forbidden surfaces enforced by the pre-commit hook.

| Implementer | Worktree branch | Tests added | ARM size impact |
|-------------|-----------------|-------------|-----------------|
| Q1 clip rect | `ux-hardening/q1-clip-rect` | `test_draw_clip.cpp` | ~100 B / host `.o` |
| Q2 footer spike | `ux-hardening/q2-footer-spike` | none (hardware-validated) | ~16 B / host `.o` |
| Q3 unused label | `ux-hardening/q3-unused-label` | `test_host_proxy_unused.cpp` (or extension) | ~16 B (static string) |

End-to-end wallclock: max of single implementer time plus integration on feature branch. Q2 has an additional hardware-feedback loop and may extend the batch if outcome B requires the deferral path.

## Spec footer

### Recipe spot-check

Q1: clip-rect recipe matches brainstorm mitigation B. Host writes per-frame, shim clamps, defaults are full-screen-safe for the harness. No regression risk on Hemispheres (already 128 px wide; bleed lands in dead space).

Q2: pot-control claim recipe is a spike, not a guaranteed fix. Outcome-driven: outcome A ships, outcome B defers. Recipe explicit about the secondary spike (drop pot-buttons if applet pot-button snap is used).

Q3: name-pointer recipe is a one-liner fix at three sites in `host_proxy.cpp`. No new infrastructure; matches the host-side initialization style already in place for bound entries.

### Per-entry verification

| Quirk | Pre-fix repro | Post-fix verification |
|-------|---------------|-----------------------|
| Q1 | Quadrants + four applets; observe x=63 pixel bleed at lane boundary | Same setup; no bleed |
| Q2 | Turn encoder on Hemispheres or Quadrants; observe footer | Outcome A: no footer; outcome B: documented defer |
| Q3 | Hemispheres + 1 applet (3 params); params page shows "-OFhW" on rows 4-16 | Rows 4-16 show `--unused--` |

### Shim prereq verification

Q1 prereqs: existing `HS::gfx_offset` and `gfx_offset_y` already live (host UX rework). New `gfx_clip_w` / `gfx_clip_h` follow the same convention. No new subsystems required.

Q2 prereqs: existing `hasCustomUi_impl` and `customUi_impl` already live in both hosts (host UX rework). Pot-control bits already defined in `vendor/distingNT_API/include/distingnt/api.h:347-356`. No new API surface.

Q3 prereqs: existing `host_proxy.cpp` initialization paths already touched per host. No new tests-infrastructure required; `harness/tests/test_host_proxy.cpp` already exists.

### Q4: Quadrants 4-button focus selector simplified to 2-button cycle

Mitigation: drop `kNT_button1` and `kNT_button2` from `Quadrants_host::hasCustomUi_impl`'s return. Replace the direct-select handlers with a cycle: `kNT_button3` advances `focused_slot_idx` by 1 (mod 4); `kNT_button4` retreats by 1 (mod 4). Buttons 1 and 2 stay unclaimed and firmware handles them per its defaults.

Affected files:

- `plugins/hosts/Quadrants_host.cpp::hasCustomUi_impl`: drop button1/2 bits from the OR.
- `plugins/hosts/Quadrants_host.cpp::customUi_impl`: replace the four direct-select if-branches with two cycle branches (forward on button3, backward on button4).
- `harness/tests/test_host_Quadrants_host.cpp`: QH1a/QH1b now assert button1/2 events leave `focused_slot_idx` unchanged. QH1c/QH1d now verify the cycle semantics including 3->0 wrap-forward and 0->3 wrap-backward. QH10 mask test updated to assert button1/2 unclaimed, button3/4 claimed. QH11 retargeted from button1 to button3 for the no-rising-edge regression.

Expected behavior: on hardware, in Quadrants Host, button3 advances which slot has the focus border and receives encoder/encoder-button events; button4 retreats. Buttons 1 and 2 perform their default firmware actions (whatever those are; likely algorithm-page navigation or no-op).

Hemispheres host out of scope: its button1/2 forward to per-slot `on_aux_button` (functional, not a wasteful direct-select).

Test plan: `make test-hosts-pilot` passes (108 assertions in Quadrants, up from 104); regression sweep green; `make arm` clean; hardware smoke confirms cycle direction matches expectation and unclaimed buttons no longer steal focus.

### Q5: Standalone per-applet button1 no longer routes to aux

Mitigation: drop `kNT_button1` from every per-applet plug-in's `hasCustomUi_impl` and remove the button1 handler from `_per_applet_runtime.h::route_custom_ui`. The applet's `on_aux_button` is no longer reachable from a standalone per-applet plug-in's customUi; firmware handles button 1 per its default (which is what the user wants).

Affected files:

- `plugins/applets/*.cpp` (49 files): `hasCustomUi_impl` returns `kNT_encoderL | kNT_encoderButtonL` instead of `kNT_encoderL | kNT_encoderButtonL | kNT_button1`. Mechanical sed.
- `plugins/applets/_per_applet_runtime.h::route_custom_ui`: the `if ((data.controls & kNT_button1) ...) { p->on_aux_button(self); }` block is removed. Comment block updated to document Q5.
- `harness/tests/test_applet_*.cpp`: mask assertions sed-updated to drop `kNT_button1` from expected masks. Two behavioral tests required hand fixes (their assertions reflected the aux-button side-effect): `test_applet_ScaleDuet.cpp::SD8` (mask bit toggle) and `test_applet_Xfader.cpp::XF7` (center_reset_enable toggle) — both flipped to assert no side-effect on button1 press. `test_applet_Scope.cpp::SC8` flipped its mask assertion to require `kNT_button1` is NOT claimed. Remaining smoke tests (~46 files) still pass without changes (their `REQUIRE(true)` assertions confirm customUi doesn't crash on button1; Q5 just means the call is a no-op).

Hosts unchanged: Hemispheres's `kNT_button1`/`kNT_button2` mapping to per-slot `on_aux_button` remains intact (functional, intentional). Quadrants's Q4 cycle mapping is unaffected.

Expected behavior change: in singular applet view (per-applet plug-in loaded as the algorithm), pressing hardware button 1 invokes the firmware default (likely algorithm-page navigation) instead of the applet's aux button. The applet's aux-button behavior is still reachable when the applet is hosted by Hemispheres (via button1/2) or Quadrants (via encoderButtonR).

Test plan: `make test-applets-pilot` passes; full regression sweep green; `make arm` clean. Hardware smoke: load any per-applet plug-in standalone; press button 1; confirm no param-edit mode activates and firmware default is observed.

## Q2 spike outcome (2026-05-22)

Both spikes ran on hardware. Result: Q2 deferred. Not shipping in this batch.

Spike 1 (claim `kNT_potL | kNT_potC | kNT_potR | kNT_potButtonL | kNT_potButtonC | kNT_potButtonR`):

- The "push: params" / "push: snap" helper text was suppressed.
- A different footer remained: algorithm name plus pot-button softkey labels ("Mapping", "Help"). These are firmware-level softkey hints.
- Regression: pushing the pot-buttons no longer escaped the host GUI to the algorithms overview, because claiming the pot-button bits routes those events to `customUi` which dropped them silently.

Spike 2 (narrow to `kNT_potL | kNT_potC | kNT_potR` only; no pot-button bits):

- The "push: params" / "push: snap" helper text returned. Pot-rotation claim alone does not suppress the helper footer.
- Softkey nav and escape gestures restored (pot-buttons no longer claimed).

Conclusion: footer suppression is keyed on the pot-button claim, not the pot-rotation claim. The two outcomes are mutually exclusive at the `hasCustomUi` level. Trading softkey navigation and overview escape for footer text suppression is a worse UX than the original quirk. Q2 deferred.

Follow-up: file an Expert Sleepers firmware feature request for an algorithm-level footer-suppression flag in `_NT_factory` (or a `customUi` variant that suppresses default firmware overlays without claiming the underlying control events). Track in a separate issue/email; out of scope for this batch.

The Q2 implementer branch `ux-hardening/q2-footer-spike` (commit `ececeba`, narrowed-mask spike 2) stays on its branch for reference but is not cherry-picked into the feature branch.
