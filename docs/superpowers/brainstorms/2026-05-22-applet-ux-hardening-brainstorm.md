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

### Q2-N: TBD

Discovery phase: deploy the hosts, exercise every applet under both Hemispheres and Quadrants, log visual or interactional quirks. Candidates that are not yet confirmed:

- Encoder accel ratio. Reentrancy probe showed `PC0=108` over ~97 logical A-value clicks: an encoder detent may produce multiple events. If applets that read raw `data.encoders[0]` advance by 1 per event, accel users may see oversteps. Not yet observed in real applet use.
- Button1/Button2 edge handling on Hemispheres host (`hh_test_inject_slot` covers edge detection; verify on hardware that double-tap and long-press behave).
- Font glyph alignment for applets that print at y = 56 (last 8-row line); if any glyph descends below 63 it clips and looks wrong.
- Quantizer / scale display widgets that draw at the bottom edge of the 64-row applet area in Hemispheres' second slot (origin_x = 128) — the second slot has no decoration to its right, so right-edge bleed is invisible there. Symmetry concern only.

Discovery is the first phase of the plan.

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
