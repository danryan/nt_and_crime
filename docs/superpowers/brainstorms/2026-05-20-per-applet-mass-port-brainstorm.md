# Per-applet mass-port brainstorm

Date: 2026-05-20
Owner: Dan
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.
Prior release: per-applet pilot (PR #12, merged 2026-05-20).

## Scope

Port the 49 Hemisphere applets not yet shipped as standalone per-applet NT
plug-ins. Pilot release shipped 6 applets plus 2 host plug-ins.

## Pre-Layer-0 gates

All four gates passed prior to Layer 0 commits.

### Gate A: vendor SHA verification

`vendor/O_C-Phazerville` HEAD = `7800d929f25868f9a8b7d3d50514532ee001649b`.
`vendor/distingNT_API` HEAD = `cd12d876dbe060859828053efab1cbc98c9df251`.
Both match the pinned SHAs at the top of this doc. PASS.

### Gate B: vendor-dep audit (49 applets)

| Category | Count | Notes |
| --- | --- | --- |
| Trivial (no vendor deps) | 28 | Header-only against shim base class |
| Vendor-dep (baseline-covered) | 18 | quant, vec-osc, cv-map, clock-mgr, lorenz headers already in shim |
| Vendor-singleton | 2 | EnigmaJr (`user_turing_machines[40]`), LowerRenz (`LorenzGeneratorManager`) |
| Needs shim work | 0 | |
| Needs vendor dep not in baseline | 0 | |

Gate threshold: halt if 5+ in last two rows. Result: 0+0 = 0. PASS.

LowerRenz is the only applet needing non-empty `VENDOR_DEPS_LowerRenz` in
the Makefile (links lorenz `.cpp` objects already shipped under
`shim/src/lorenz/`).

### Gate C: Batch 4 per-applet 10x-ticks categorization

The host harness runs vendor `Controller()` 10 times per NT `step()`. A
single rising edge keeps `clocked[ch]=true` across all 10 inner calls.
Each Batch 4 applet categorized individually below.

| Applet | Clock in Controller | Per-tick timing | Internal phase | Test coverage shape |
| --- | --- | --- | --- | --- |
| Metronome | No | No | No | Bus-level fire-count safe |
| ResetClock | Yes | Yes | Yes | State-injection only |
| Shuffle | Yes | Yes | Yes | State-injection only |
| Xfader | No | No | No | Bus-level fire-count safe |
| Scope | Yes | Yes | No | Model the multiplier |
| ClkToGate | Yes | No | No | Model the multiplier |
| ClockSkip | Yes | No | No | Model the multiplier |
| PolyDiv | Yes | No | Yes | State-injection only |

ResetClock, Shuffle, and PolyDiv carry the highest test-prompt risk: each
applet's Controller is entangled enough with the 10x inner-tick loop that
bus-level fire-count assertions are unsound. Their implementer prompts
state explicitly "state-injection only" coverage and forbid bus-level
fire-count assertions.

### Gate D: file-scope mutable state audit

49 vendor applet headers scanned for `static` (non-const) and
`namespace foo {` mutable globals.

Result: 49/49 clean from a two-instance hosting perspective. The 8
applets with `static` declarations all use `static constexpr` or
`static const` (read-only lookups). LowerRenz pulls in a singleton
that is intentionally shared within a single host context; per-applet
.o isolation makes each plug-in own its own singleton, preserving the
intent.

No Layer 0.1-style mitigation needed beyond what Layer 0.1 already
covers in the runtime header.

## Status per applet

All 49 applets are listed in `applet_indices.h` and already compile
against the shim baseline (as part of the bundled `Hemispheres.o` and
`Hemispheres2.o` variants). The mass-port release wraps each as a
standalone per-applet plug-in using the pilot template.

| Batch | Count | Applets |
| --- | --- | --- |
| 1a | 5 | AttenuateOffset, Binary, Button, Logic, Switch |
| 1b | 7 | Brancher, Burst, Calculate, EnvFollow, GameOfLife, GateDelay, GatedVCA |
| 1c | 8 | RndWalk, Schmitt, ShiftGate, Slew, Stairs, TLNeuron, Trending, Voltage |
| 2 | 3 | VectorEG, VectorMod, VectorMorph |
| 3 | 13 | DualQuant, OffsetQuant, MultiScale, ScaleDuet, EnsOscKey, Calibr8, Carpeggio, Chordinator, EnigmaJr, Pigeons, Squanch, Shredder, Strum |
| 4 | 8 | Metronome, ResetClock, Shuffle, Xfader, Scope, ClkToGate, ClockSkip, PolyDiv |
| 5 | 5 | ADEG, ADSREG, RunglBook, LowerRenz, Combin8 |

Total: 49.

## Exclusions

WTVCO, DuoTET, EbbAndLfo remain deferred. They require CMSIS-DSP and
have a separate dep-port phase pending.

## Layer 0.4 deferral

Host slot-selector UX rework and parameter proxying tracked at
`docs/superpowers/prompts/2026-05-20-host-ux-rework-kickoff.md`. The
49 applet ports do not depend on it; applets ship with the current
raw-slot-index host UI.
