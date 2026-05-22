# Brainstorm: vendor applet UX hardening batch

Date: 2026-05-22
Owner: Dan
Predecessor: Host UX rework PR #15 (merged at squash `<TBD>`). Mass-port release at PR #14.
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`. Re-verify at kickoff.

## Scope

A batch release to harden the UX of the existing 49 per-applet plug-ins on the two composer hosts (Hemispheres, Quadrants). Targets visible quirks the user discovers while running the host UX rework's selector + parameter proxying in practice. The hosts and the proxy aggregator are not edited; the fixes live in the shim's drawing wrappers, in per-applet plug-ins, or in vendor compat headers, depending on the quirk.

This is a discovery-driven phase: we have one confirmed quirk (Quadrants right-edge bleed) and an open invitation to enumerate the rest while exercising the hardware.

## Why a batch, not one-quirk-per-release

Per-quirk releases would spam the firmware-deploy cycle for cosmetic fixes. Bundle a small set, ship once. Bound the scope tightly so the batch closes in a few sessions.

## Known quirks

### Q1: Quadrants right-edge bleed (confirmed 2026-05-22)

When four applets render in Quadrants Host, the rightmost pixel column of each applet (local x = 63 in vendor coords) bleeds 1 px into the neighboring lane's column. Visible on hardware after host UX rework deploy.

Provisional root cause: vendor applets target a 64-wide panel and many draw routines emit at `x = 63` or `x = 64` (`gfxFrame(0, 0, 64, 64)`, `gfxLine(0, y, 64, y)`, text rendering with a 6-wide glyph at x = 58). The shim's draw wrappers add `gfx_offset` (lane origin x) before forwarding to the global `graphics` adapter; no clip rect enforced. In Hemispheres each slot owns 128 px (64-wide applet plus 64-wide gap), so bleed lands in unused screen real estate. Quadrants packs 64 px per lane with zero gap; bleed lands in the next lane.

Mitigation options (pick during spec):

- A. Shim-side clip: `gfx*` helpers clamp to `[gfx_offset, gfx_offset + 64)` for x and `[gfx_offset_y, gfx_offset_y + 64)` for y. Universal; zero per-applet edits. Risk: hides applet bugs that were tolerated under the bleed.
- B. Host-set clip rect: host writes `gfx_clip_w` / `gfx_clip_h` before each `render_view`; shim wrappers clip against it. Most flexible; supports future layouts (asymmetric grids).
- C. NT-API-level clip via `NT_screen` access in the drawing wrapper. Tight to firmware ABI; bigger risk of regression.
- D. Per-applet fix the vendor-source draw routines. Spec exclusion in mass-port said no applet edits; this batch can lift that locally.

Recommend B (host-set clip rect) as the spec leans into it: future composer hosts get a free knob without coordinated applet edits.

### Q2: Encoder-turn footer obscures bottom of display (confirmed 2026-05-22)

When the user turns an encoder, the NT firmware overlays a footer at the bottom of the screen displaying helper text ("push: params", "push: snap" mapping hints). The footer obscures the bottom rows of whatever the algorithm is rendering. The text is low-value: it documents the encoder-push shortcut, which the operator learns once and then does not need re-confirmed every turn.

Provisional root cause: firmware-level UI overlay. Likely triggered by encoder-turn events outside the algorithm's `draw()`. Not under direct algorithm control. Investigation needed:

- Is there an NT API or `_NT_factory` flag that suppresses the footer? Search `vendor/distingNT_API/include/distingnt/api.h` for footer / hint / overlay hooks.
- Does `customUi` claim suppress the overlay? The host plug-ins already implement `customUi` for parameter proxying; the proxy path may already be in scope.
- Falls under "vendor ABI semantics" risk in the abort criteria. If no shim/host-side mitigation exists, this quirk gets deferred to a firmware feature request rather than fixed here.

Fix surface guess: host plug-in `customUi` flag, or none (firmware-side).

**2026-05-22 spike outcome: deferred.** Two hardware spikes ran (`ux-hardening/q2-footer-spike` commits `fe31a7c` then narrowed to `ececeba`). Suppression is keyed on the pot-button claim (`kNT_potButtonL/C/R`), which doubles as the firmware softkey-nav input. Claiming pot-buttons hides the helper footer text but breaks the user's ability to escape the host GUI to the algorithms overview and removes Mapping/Help navigation. The trade-off is worse than the original quirk. Pot-rotation-only claims have no effect on the footer. Outcome: Q2 deferred to a firmware feature request (algorithm-level footer-suppress flag in `_NT_factory`). See spec footer for the full spike record.

### Q3: Unused parameter rows show garbage label ("-OFhW")

In the algorithm's parameter page, parameter slots that are not currently bound to a proxied vendor parameter display the label "-OFhW" (or similar garbage). Should read something obviously inert like `--unused--`.

Provisional root cause: per CLAUDE.md `numParameters must match actual valid table range`, firmware reads `inst->parameters[i]` for `i` in `[0, numParameters)` without validating each entry. If the host returns a larger `numParameters` than actually populated, the trailing entries are phantom `_NT_parameter` structs whose `name` field points at uninitialized memory. Either `numParameters` exceeds the populated range, or all entries get a placeholder string that prints as "-OFhW".

Fix surface guess: host plug-in's parameter-table construction (`Hemispheres_host.cpp::calculateRequirements_impl` / parameter initialization) or the proxy aggregator's name-resolution path. Either trim `numParameters` to active range, or write `--unused--` into the `name` field of unbound entries.

Note: this contradicts the mass-port batch's "no host edits" exclusion. The exclusion was for behavioral changes; a name-string fix on unbound entries is cosmetic. Spec phase decides whether to lift the exclusion locally or defer Q3 to a separate phase.

### Q4: Quadrants 4-button focus selector wastes buttons (added 2026-05-22)

Quadrants Host's `customUi_impl` currently claims `kNT_button1 | kNT_button2 | kNT_button3 | kNT_button4` and uses each button as a direct-select for one of the four slots (button1 -> slot 0, ..., button4 -> slot 3). This obscures all four firmware buttons' default uses for the trivial benefit of single-press random access to any slot.

Better mapping: claim only `kNT_button3` (advance focused slot forward) and `kNT_button4` (retreat focused slot). Buttons 1 and 2 stay unclaimed and firmware handles them per its defaults. Worst-case slot access is two presses instead of one; the benefit is freeing two firmware buttons.

Fix surface guess: `plugins/hosts/Quadrants_host.cpp::hasCustomUi_impl` and `customUi_impl` only. Existing `qq_test_inject_slot` host test seam covers regression. Hemispheres host is out of scope (its button1/2 map to slot 0/1 aux-button forwarding, which IS functional and not wasteful).

### Discovery complete (2026-05-22)

Sweep complete on hardware: three confirmed quirks (Q1 bleed, Q2 encoder-turn footer, Q3 unused-param labels). No additional quirks observed during the cycle through all 49 per-applet plug-ins under both Hemispheres and Quadrants hosts. The candidate list below remained unconfirmed and is dropped from this batch's scope (re-eligible for a future hardening pass if observed):

- Encoder accel ratio (no overstep observed).
- Button1/Button2 edge handling (double-tap and long-press behaved correctly).
- Font glyph y = 56 clipping (not observed).
- Hemispheres second-slot symmetric right-edge bleed (symmetry concern only; not observed as a problem).

Three quirks is comfortably under the 6-quirk abort threshold. Proceed to Stage 2 (spec).

## Out of scope

- Adding new applets. This batch only hardens existing ones.
- Changing the `HemiPluginInterface` ABI (no version bump).
- Changing the host plug-ins (`Hemispheres_host.cpp`, `Quadrants_host.cpp`) or the proxy aggregator (`host_proxy.{h,cpp}`).
- Cross-applet behavioral changes (state retention across applet swap, etc.) — different phase.

## Risks and abort criteria

- Discovery yields zero additional quirks beyond Q1. The batch shrinks to a single-quirk release. Acceptable; ship it.
- Discovery yields more than ~6 quirks. Halt; carve sub-phases by category (drawing vs input vs state) rather than bundle all into one batch.
- A quirk's root cause is in `HemiPluginInterface` semantics rather than vendor code. Halt; the fix needs an ABI revision; deferred to a future release.
- Mitigation A (shim-side clip) breaks a vendor applet that relied on out-of-bounds writes for a visual effect. Switch to mitigation B (host-set clip rect) which lets the host opt in; both Hemispheres and Quadrants set the 64x64 rect, leaving room for future hosts that want wider lanes.

## Phased order

1. Discovery: deploy the host UX rework artifacts, exercise every applet under both hosts, log quirks into a "Q2..QN" section of this brainstorm. Time-box to one session.
2. Spec: design the chosen mitigation(s) per quirk. Recipe + per-quirk fix entries.
3. Plan: worklist; parallel where independent.
4. Implementer dispatch where parallel-safe; otherwise sequential.
5. Integration + smoke + PR.
