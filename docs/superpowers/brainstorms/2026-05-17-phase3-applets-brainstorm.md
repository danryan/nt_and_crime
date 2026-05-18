# Brainstorm: Phase 3 Hemisphere applet ports

Date: 2026-05-17
Status: Draft (awaiting review)
Owner: Dan
Worktree: `.worktrees/phase3-applets-plan`
Branch: `dr/phase3-applets-plan`

## Vendor pin

- Submodule path: `vendor/O_C-Phazerville`
- Pinned SHA: `7800d929f25868f9a8b7d3d50514532ee001649b`
- Pinned commit: `version bump v1.13.1` (2026-04-20)
- Applet header count under that SHA: 87 `.h` files in `software/src/applets/`, plus `Boilerplate.h.txt` (template, not a real applet).

Any re-pin invalidates the inventory below. Rerun this brainstorm, do not patch it.

## Audit summary

The five primary-reference files were read in full. The recipe expressed in code is consistent across all 14 existing ports. Key audit findings:

- The factory registration table is in `shim/include/HemispheresFactory.h`, not in `applets/Hemispheres.cpp`. The prompt's claim that the table is in `Hemispheres.cpp` is incorrect; `Hemispheres.cpp` is 13 lines and contains only the `NT_HEMISPHERES_PLUGIN` macro and the `get_applet_impl` test seam. Integration touches three files: `shim/include/applet_indices.h` (enum), `shim/include/HemispheresFactory.h` (`#include`, factory table entry, `kMaxAppletSize` and `kMaxAppletAlign` `cmax` chain, `applet_enum_strings` entry), and no per-applet edit to `Hemispheres.cpp`.
- The enum in `applet_indices.h` lists applets in alphabetical order, not registration order. Inserts are alpha-positioned; not pure appends. This still parallelises cleanly because the integration step is sequential.
- The 14 existing ports are: Empty (shim-local, `shim/include/Empty.h`), AttenuateOffset, Brancher, Burst, Button, Calculate, ClkToGate, Compare, Cumulus, GateDelay, GatedVCA, Logic, Slew, TLNeuron. The vendor headers used for the 13 vendor-backed ports all exist under the pinned SHA.
- Test seam pattern: tests get a `HemisphereApplet*` via `hem_test::get_applet(hi, side)`, inject state via `OnDataReceive(pack_<name>(...))`, drive the bus via `set_gate` / `hold_gate` / `set_cv`, step via `step_n_frames`, and read via `read_gate_at` / `read_cv_at`. Applets with empty `OnDataRequest` (Button, GatedVCA) skip the pack helper entirely and assert `OnDataRequest() == 0` for the round-trip case.
- Bit-layout encoding follows vendor `OnDataRequest` exactly. Gap bits between vendor `Pack` fields must be explicitly zeroed in the pack helper (see Cumulus bits 11..12). Biased fields use the same bias the vendor uses on `OnDataReceive` so round-trips reproduce the input.

The pattern generalises. Phase 3 in-scope applets *appear* to need no harness change; this is verified per applet during spec authoring rather than assumed up front. The LowerRenz and Xfader demotions during walkthrough review (DSP-singleton and `millis()` gaps respectively) are the proof that overconfidence here is expensive.

## Pair-applet decision

Phase 3 ports applets as single-applet plug-ins only. Pair-applet variants (the `LogicCalculate` canary in commit `3b0c65b`) are deferred.

Rationale:

- The pair-shim path is a fresh code surface (commit `3b0c65b`, one applet pair). Track A coverage of single-applet hosts is the larger gap.
- Pair-applet tests would need a new shim type, a new factory entry per pair, and a new bus layout. None of that exists yet at scale.
- A pair-shim build per applet doubles the build matrix without doubling test coverage of applet behavior. Behavior is identical to the single-applet host case under default left-encoder vs right-encoder routing.

Once Phase 3 closes, a separate Phase 4 brainstorm can decide if any pair variants are worth shipping with their own tests.

## Out-of-scope category list

This is the canonical exclusion list for Phase 3 and supersedes any prose in `docs/superpowers/specs/2026-05-17-applet-tests-track-a-design.md` (which never declared a formal exclusion set).

- E - Quantizer-dependent. Out of scope unless a quantizer shim lands first.
- F - Scales-dependent. Out of scope unless `OC::Scales` ships.
- G - MIDI-dependent. Out of scope; no MIDI infrastructure.
- H - Audio-DSP synths. Out of scope; the shim writes Out() once per Controller call, so audio-rate output is undersampled.
- I - Other global, hardware, full-screen, or system infrastructure. Out of scope.

Categories that ship in Phase 3:

- A - Trivial. No quantizer, scales, MIDI, audio. Simple state. Mirrors the existing-port pattern directly.

Categories deferred to a future phase. Detailed categorisation of categories B, C, D, and other not-yet-ported applets is out of scope for this brainstorm. A separate Phase 4 brainstorm will refine it. Stub definitions:

- B - Modulate-heavy. Folded into A; the Phase 2 ports already cover the CV-input case under the existing harness.
- C - Time-sensitive (uses `OC::CORE::ticks` or ms-throttling).
- D - Stateful (sequencers, recorders).

## Inventory

87 vendor applet headers under the pinned SHA. 13 are already ported. 74 are candidates. Only the Phase 3 in-scope set (category A) is enumerated below with bit counts and analogues. Out-of-scope categories (E through I) are listed by name. All other unported applets are deferred to Phase 4 and will be categorised in that brainstorm.

Already ported (Phase 1 and Phase 2):

| Applet | Vendor header |
| --- | --- |
| Empty | `shim/include/Empty.h` (shim-local) |
| AttenuateOffset | `vendor/.../AttenuateOffset.h` |
| Brancher | `vendor/.../Brancher.h` |
| Burst | `vendor/.../Burst.h` |
| Button | `vendor/.../Button.h` |
| Calculate | `vendor/.../Calculate.h` |
| ClkToGate | `vendor/.../ClkToGate.h` |
| Compare | `vendor/.../Compare.h` |
| Cumulus | `vendor/.../Cumulus.h` |
| GateDelay | `vendor/.../GateDelay.h` |
| GatedVCA | `vendor/.../GatedVCA.h` |
| Logic | `vendor/.../Logic.h` |
| Slew | `vendor/.../Slew.h` |
| TLNeuron | `vendor/.../TLNeuron.h` |

Phase 3 in-scope (category A, ships in this phase). 15 applets:

| Applet | Vendor header | Bits | Closest analogue | Notes |
| --- | --- | --- | --- | --- |
| Binary | `Binary.h` | 0 | Compare + GatedVCA | Threshold gate emitter, single CV in, gate out. Empty `OnDataRequest`. |
| ClockDivider | `ClockDivider.h` | 32 | ClkToGate | Per-side clock divider; div + divmult, both +32-biased. |
| ClockSkip | `ClockSkip.h` | 14 | Brancher | Probabilistic clock pass-through; uses RNG. |
| EnvFollow | `EnvFollow.h` | 16 | Slew | CV-to-CV envelope follower; per-side gain plus duck plus speed. |
| PolyDiv | `PolyDiv.h` | 20 | ClkToGate | Per-side independent integer divider. |
| ProbabilityDivider | `ProbabilityDivider.h` | 40 | Brancher + Cumulus | Random clock pass with 4 weight slots; uses RNG. |
| ResetClock | `ResetClock.h` | 17 | ClkToGate | Length-1 biased plus offset plus spacing. |
| RndWalk | `RndWalk.h` | 31 | Slew + Brancher | Random-walk CV; 6 fields. |
| RunglBook | `RunglBook.h` | 16 | Slew | Runglerised CV; single threshold field, 8-bit shift register. |
| Schmitt | `Schmitt.h` | 32 | Compare | Per-side schmitt trigger; two 16-bit thresholds. |
| ShiftGate | `ShiftGate.h` | 32 | Cumulus + ClkToGate | Gate shift register; gap bits 10..15. |
| Stairs | `Stairs.h` | 8 | Cumulus | Stepped CV up, down, or up-down on Clock(0). |
| Switch | `Switch.h` | 0 | GatedVCA + Button | Multi-input switch driven by Clock or CV. Empty `OnDataRequest`. |
| Trending | `Trending.h` | 16 | Compare | Slope detector; per-side assign plus sensitivity. |
| Voltage | `Voltage.h` | 21 | AttenuateOffset | Per-side CV constant emitter; gap bit at position 9. |

Shim-infra audit notes:

- LowerRenz dropped from Phase 3. Pulls `HSLorenzGeneratorManager` singleton plus `streams::LorenzGenerator` DSP from `streams_lorenz_generator.{h,cpp}`. Singleton and DSP are not in the current shim. Reclassify under Phase 4 (DSP-singleton dep).
- Xfader dropped from Phase 3. Uses Arduino `millis()` in its Controller (`Xfader.h:44`). No `millis()` stub in the shim's `Arduino.h`. Even with a stub, the rate-driven balance ramp needs roughly 2000 step() calls to traverse the 16-bit balance range; primary behavior is out of host-test budget. Reclassify under Phase 4 (shim-stub plus tick-budget).

Out of scope - category E (quantizer-dependent):

ASR, Carpeggio, Chordinator, DualQuant, DuoTET, EnigmaJr, EnsOscKey, MultiScale, OffsetQuant, Pigeons, ProbabilityMelody, ScaleDuet, Seq32, SeqPlay7, Shredder, Squanch, Strum, SwitchSeq, TB3PO, TwoRings, Calibr8.

Quantizer dependency is identified by use of `Quantize(...)`, `HS::QuantizerLookup`, `OC::Scales::GetScale`, `MIDIQuantizer::CV`, or `HS::TuringMachine` global state. EnigmaJr also depends on `HS::TuringMachine` globals; treat as category E.

Out of scope - category F (scales-only, no quantizer call):

ShiftReg. Initialises `OC::Scales::GetScale(OC::Scales::SCALE_SEMI)`; out of scope until `OC::Scales` ships.

Out of scope - category G (MIDI-dependent):

MidiLoop, hMIDIIn, hMIDIOut.

Out of scope - category H (audio-DSP synths):

BootsNCat, BitBeat, BugCrack, DrLoFi, WTVCO. Each runs an audio-rate `VectorOscillator` or filter graph and emits sample-rate audio via `Out()`. The shim calls Out() once per Controller tick (about 10 per 32-sample buffer), so audio-rate behavior is undersampled and not observable through the bus.

Out of scope - category I (other system, hardware, or global state):

- MiniSeq - reads global `OC::user_patterns[]` array.
- Scope - display-only; empty `OnDataRequest`.
- Tuner - depends on `OC::FreqMeasure` hardware driver.
- ClockSetup, ClockSetupT4 - clock-source configuration applets, not user-selectable Hemisphere applets in the Phazerville UI flow; both also pull `usbMIDI`. Excluded as system applets per Phazerville convention.

## Phase ordering rationale

Phase 3 ships 15 category-A applets. Categories B, C, D, and unblocking work for E through I are out of scope for this brainstorm and will be addressed by Phase 4.

Phase 3's success criterion is that 15 category-A ports land in parallel under the existing harness with no new helper machinery. This validates the pattern at scale before later phases apply pressure to the recipe or to the shim.

## Reference

- Primary reference (recipe ground truth):
  - `applets/Hemispheres.cpp`
  - `shim/include/applet_indices.h`
  - `shim/include/HemispheresFactory.h`
  - `harness/tests/applet_test_helpers.h`
  - `harness/tests/applet_test_helpers.cpp`
  - `harness/tests/test_hemispheres.cpp`
- Secondary reference (intent and history):
  - `docs/superpowers/specs/2026-05-17-applet-tests-track-a-design.md`
  - `docs/superpowers/plans/2026-05-17-applet-tests-track-a-phase2-parallel.md`
- Vendor headers: `vendor/O_C-Phazerville/software/src/applets/*.h` at SHA `7800d929`.
- Firmware index: `https://firmware.phazerville.com/App-and-Applet-Index#hemisphere-applets`.

## Open questions for review

1. Phase 3 in-scope set is 15 applets (17 minus LowerRenz and Xfader, both blocked on shim infra). Acceptable.
2. Pair-applet variants deferred entirely. Confirm.
3. The Phase 2 spec did not declare a formal exclusion list; this brainstorm declares one (categories E through I). Confirm this is the canonical list for the Phase 3 plan.

## Commitments

- Phase 4 categorisation is out of scope for this brainstorm. A separate Phase 4 brainstorm will refine it.
