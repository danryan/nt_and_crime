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

### Q1: Quadrants right-edge bleed (host-set clip rect)

Mitigation: B from brainstorm. Host plug-in writes a per-frame clip rect into shim globals; shim drawing helpers clamp to the rect.

Affected files (writable surface for Q1 implementer):

- `shim/include/HSUtils.h`: declare `extern int gfx_clip_w;` and `extern int gfx_clip_h;` in the `HS` namespace, beside the existing `gfx_offset` / `gfx_offset_y`.
- `shim/src/globals.cpp`: define `HS::gfx_clip_w = 256;` and `HS::gfx_clip_h = 64;` as defaults (full screen, harness-safe).
- `shim/include/HemisphereApplet.h`: clamp emitted pixels in the `gfx*` helpers (`gfxPixel`, `gfxLine`, `gfxFrame`, `gfxRect`, `gfxBitmap`, `gfxPrint`, etc.) against `[HS::gfx_offset, HS::gfx_offset + HS::gfx_clip_w)` x `[HS::gfx_offset_y, HS::gfx_offset_y + HS::gfx_clip_h)`. Pixels with x or y outside the rect are dropped silently. The clamp is per-emit; bounding-box culls (entire shape outside) short-circuit.
- `plugins/hosts/Hemispheres_host.cpp`: before each `render_view` call, set `HS::gfx_clip_w = 64; HS::gfx_clip_h = 64;`. Restore prior values (or reset to default) after the call to avoid leaking the lane-sized clip to firmware UI.
- `plugins/hosts/Quadrants_host.cpp`: same as Hemispheres host.
- `harness/tests/test_draw_clip.cpp`: new test file. Cases:
  - default rect (clip_w=256, clip_h=64): a pixel emit at (200, 30) is delivered to the framebuffer.
  - host-set rect (clip_w=64, clip_h=64, offset=64): an emit at vendor coord (63, 30) lands at screen (127, 30); an emit at vendor coord (64, 30) is clipped.
  - lower-edge clip: an emit at vendor (10, 64) is clipped; an emit at vendor (10, 63) lands.
  - upper-edge clip: an emit at vendor (10, -1) (vendor draws above the lane) is clipped.

Forbidden surface for Q1 implementer: vendor source, applet `.cpp`/`.h` under `plugins/applets/`, `shim/src/host_proxy.cpp`, `shim/include/host_proxy.h`.

Expected behavior change: in Quadrants Host with four applets, the rightmost column of each lane no longer bleeds into the neighbor's leftmost column. Visible verification on hardware: deploy `Quadrants_host.o` plus any per-applet plug-in that draws to `x = 63` (Calculate8, EuclidX, ProbabilityDivider with a tall display); cycle slots 1-4; observe no inter-lane pixel bleed at slot boundaries.

Performance note: the clamp runs per draw call, not per pixel for line/frame primitives. Bounding-box culls go first (entire shape outside rect = early return). Per-pixel clamp only matters for `gfxBitmap` and `gfxPrint`. Measure no regression in host-test draw benchmarks.

Test plan: `make test-draw` and `make test-draw-shape` plus new `make test-draw-clip` all green; ARM `.text` budget unchanged within ~100 bytes per host `.o`.

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
