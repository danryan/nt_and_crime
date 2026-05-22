# Plan: vendor applet UX hardening batch

Date: 2026-05-22
Owner: Dan
Brainstorm: `docs/superpowers/brainstorms/2026-05-22-applet-ux-hardening-brainstorm.md`
Spec: (written after stage 1 discovery)
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.

## Parallelization strategy

Discovery (stage 1) is sequential and hardware-gated; only Dan can run it. Spec authoring (stage 2) and per-quirk fixes (stage 3) parallelize per quirk once the spec is locked. Integration + smoke + PR is sequential.

| Stage | Work | Mode | Gate |
|-------|------|------|------|
| 1 | Discovery sweep on hardware | Dan | none |
| 2 | Spec authoring (Q1 + any new) | sequential on feature branch | stage 1 complete |
| 3 | Per-quirk fix implementer(s) | parallel where independent | stage 2 merged |
| 4 | Integration + full sweep | sequential | stage 3 complete |
| 5 | Hardware smoke + PR | Dan | stage 4 green |

## Stage 1: discovery sweep

### Setup

1. Deploy current hosts: `make deploy-sysex SYSEX_PLUGIN=build/arm/Hemispheres_host.o SYSEX_ID=0` then same for Quadrants_host.o.
2. Build a preset with each host visible: Hemispheres + 2 applets in slots 1-2; Quadrants + 4 applets in slots 1-4.
3. Cycle through every per-applet plug-in by swapping the enum selector on each host. Observe under both hosts.

### Recording

For each quirk, capture in the brainstorm's "Q2..QN" section:

- Affected applet(s) (or "all" if generic).
- Host(s) where observed (Hemispheres, Quadrants, both).
- Reproducer (preset state + steps).
- Visual or behavioral signature.
- Suspected root cause (skip if not obvious).

Time-box to one session. If you find more than 6 quirks, halt and reassess scope per brainstorm abort criteria.

### Done when

- Brainstorm Q1..QN section is populated with the observed set.
- Each quirk has a 1-line "fix surface" guess (shim wrapper / per-applet / vendor diff / abi).

## Stage 2: spec

Write `docs/superpowers/specs/2026-05-XX-applet-ux-hardening-design.md`:

- Canonical recipe for the chosen mitigation per quirk category (e.g. "host-set clip rect" applies to all 64x64 bleed quirks).
- Per-quirk entries: affected files, expected behavior change, test plan.
- Spec footer: recipe spot-check, per-entry verification, shim prereq verification.

For Q1 specifically the recipe is host-set clip rect (mitigation B from the brainstorm). Concretely:

- Add `HS::gfx_clip_w` and `HS::gfx_clip_h` globals beside the existing `HS::gfx_offset` / `HS::gfx_offset_y` in `shim/include/HSUtils.h` and define them in `shim/src/globals.cpp`.
- Host plug-ins set `HS::gfx_clip_w = 64; HS::gfx_clip_h = 64;` before each `render_view` call (alongside the existing `HS::gfx_offset` write).
- Shim's `graphics` adapter (or the `HemisphereApplet::gfx*` helpers) clamps emitted pixels to `[gfx_offset, gfx_offset + gfx_clip_w)` x `[gfx_offset_y, gfx_offset_y + gfx_clip_h)`. Default (when host did not set, e.g. test harness) is the full screen rect.
- Unit tests on the clip in `harness/tests/test_draw_clip.cpp`.

## Stage 3: per-quirk fixes

For each quirk identified in stage 1, dispatch an implementer (parallel where the writable surfaces are disjoint). Each implementer:

- Reads the spec entry for its quirk.
- Writes failing tests first.
- Implements the fix on a worktree branched off the feature branch.
- Reports back with worktree path, branch, commit SHA, test output, ARM size delta if any.

The Q1 implementer's writable surface: `shim/include/HSUtils.h`, `shim/src/globals.cpp`, the `graphics` adapter (`shim/src/graphics.cpp` and the `HemisphereApplet::gfx*` helpers in `shim/include/HemisphereApplet.h`), the two host plug-ins (to set the clip rect), and the new test file. Forbidden surface: applet .cpp files, host_proxy, vendor.

## Stage 4: integration

1. Cherry-pick implementer commits onto feature branch.
2. `make test-applets`, `make test-buses`, `make test-draw`, `make test-draw-shape`, `make test-host-proxy`, host plug-in tests, applet tests.
3. `make arm`. Sizes recorded per `.o`.
4. Symbol audit on changed `.o`s.

## Stage 5: hardware smoke + PR

Hardware sweep: re-deploy hosts; re-run the original quirk reproducers; verify each quirk is gone; verify no regression on the unaffected applets.

PR body: list of quirks fixed, before/after screenshots if useful, link to brainstorm and spec, test plan checklist.

## Worktree-dispatch checklist (parent agent, stage 3)

If multiple quirks dispatch in parallel:

1. Confirm feature branch HEAD has stage 2 committed. Current feature branch: `dr/ux-hardening-batch`.
2. For each implementer: `git worktree add .worktrees/ux-hardening-<quirk-id> -b ux-hardening/<quirk-id> dr/ux-hardening-batch`.
3. Verify spec doc reachable; init submodules in each worktree (`git submodule update --init --recursive --depth=1`).
4. Constrain writable surface per quirk in the implementer prompt.
5. Pre-commit hook enforcement: the project pre-commit hook at `.git/hooks/pre-commit` has cases for the three Stage 3 branches that hard-reject staged files outside each implementer's writable surface. The hook also rejects commits on branches not derived from `dr/ux-hardening-batch`. Surfaces (regex from the hook):
    - `ux-hardening/q1-clip-rect`: `shim/include/HSUtils.h`, `shim/src/globals.cpp`, `shim/include/HemisphereApplet.h`, `plugins/hosts/Hemispheres_host.cpp`, `plugins/hosts/Quadrants_host.cpp`, `harness/tests/test_draw_clip.cpp`, `Makefile`.
    - `ux-hardening/q2-footer-spike`: `plugins/hosts/Hemispheres_host.cpp`, `plugins/hosts/Quadrants_host.cpp`.
    - `ux-hardening/q3-unused-label`: `shim/src/host_proxy.cpp`, `harness/tests/test_host_proxy.cpp` or `harness/tests/test_host_proxy_unused.cpp`, `Makefile`.

## Pre-PR quality gate

- All applet tests green.
- All host plug-in tests green.
- `make arm` clean; no new unresolved symbols.
- markdownlint clean on new docs.
- Hardware smoke passes per quirk.
