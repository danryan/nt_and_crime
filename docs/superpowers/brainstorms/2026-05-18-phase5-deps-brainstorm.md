# Brainstorm: Phase 5 vendor dep port batch

Date: 2026-05-18
Status: Active
Owner: Dan
Branch: `dr/phase5-deps-plan`
Worktree: `.worktrees/phase5-deps-plan`
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.
Kickoff: `docs/superpowers/prompts/2026-05-18-phase5-deps-kickoff.md`.

## Scope

Land six vendor dep ports plus shared Layer 0 shim infrastructure plus the
time-injection harness helper on `dr/phase5-deps-plan`. No applet ports land
in Phase 5. The 31 existing applets continue to pass at baseline (155 cases /
1803 assertions).

The pivot from cat-C applet ports to vendor dep ports was decided in the
kickoff prompt after a preflight audit found the cat-C applet inventory at the
Phase 4 shim surface is exhausted (only 4 of 7-9 candidates fit, below the
abort budget's 5-applet floor). Two vendor subsystems gate roughly two-thirds
of the remaining unported applet inventory: VectorOscillator stack and
Quantizer subsystem. Phase 5 ships those two plus four smaller deps and
returns Phase 6 to applet ports with a much larger fittable inventory.

## Categorization (six implementer tasks, possibly seven)

After bundling VectorOscillator + WaveformManager + RelabiManager into a
single implementer (load-bearing decision recorded in kickoff), the implementer
count is six, with a possible split of dep-quant into two implementers per
preflight LoC. Preflight decided: dep-quant ships MONOLITHIC (vendor LoC
~1010, under the 1500 threshold).

Final Layer 1 + 1.5 dispatch: **six implementers, all parallel**.

| Slug | Cat | Vendor LoC | Transitive | Shim wrap | Unblocks (Phase 6) |
|---|---|---|---|---|---|
| dep-vec-osc | A1 | 913 (HSVectorOscillator + WaveformManager + waveform_library + HSRelabiManager) | vendor util_math.h symbols | ~100 | VectorLFO, VectorEG, VectorMod, VectorMorph, Relabi, drum/synth applets |
| dep-lorenz | A1 | 450 (streams_lorenz_generator + HSLorenzGeneratorManager) | streams_resources.{h,cpp} 1542 (mostly inert data; only `lut_lorenz_rate` is a live extern); util_macros.h | ~50 | LowerRenz |
| dep-tideslite | A1 | 302 (tideslite + util_phase_extractor) | util_pattern_predictor.h 95 | ~50 | EbbAndLfo, WTVCO |
| dep-clock-mgr | A2 | 367 (HSClockManager full) | HSMIDI.h 412, `<functional>`, `<vector>` | ~50 | Metronome immediately; all clock-aware applets long term |
| dep-quant | A1 | 1010 (braids_quantizer + braids_quantizer_scales + OC_scales + MIDIQuantizer 30 extracted from HSMIDI.h) | FS.h stub for SD scala paths | ~250 (HS:: quantizer glue: `QuantEngine`, `Quantize`, `QuantizerLookup`, `QuantizerConfigure`, `GetScale/SetScale`, `GetRootNote`, `SetRootNote`, `NudgeScale`, `QuantizerEdit`, `GetLatestNoteNumber`, `QUANT_CHANNEL_COUNT`, `q_engine[]` global; `OC::Scales::GetScale`, `OC::Scales::NUM_SCALES`, `OC::Scales::SCALE_SEMI`, `OC::SemitoneQuantizer`, `OC::DAC::kOctaveZero`) | Pigeons, Strum, Shredder, Carpeggio, Squanch, Chordinator, DualQuant, EnigmaJr, OffsetQuant, MultiScale, ScaleDuet, DuoTET, EnsOscKey, Calibr8 |
| dep-cv-map | A2 | 266 (vendor CVInputMap full) | bjorklund.{h,cpp} 1401, util/clkdivmult.h 131 | ~30 (replaces 20-LoC shim stub) | Combin8 |

Categorization key:
- **A1**: pure logic with no shared base-class touches.
- **A2**: replaces an existing shim stub; surface contract must be preserved
  for the 31 baseline applets.

## Load-bearing decisions (recorded here, not deferred)

1. **VectorOscillator + RelabiManager bundled** into one implementer task.
   Relabi is 88 LoC and serves no purpose without VectorOscillator. Splitting
   them into two implementers risks cross-dep symbol collisions in Layer 2 at
   the cost of restarting. Ship as one implementer with one allowed-surface
   boundary covering both files.
2. **dep-quant MONOLITHIC** per preflight LoC count (1010 vendor LoC, under
   the 1500 threshold). Single dep-quant implementer with allowed surface
   spanning `shim/include/quant/`, the HS:: quantizer surface in
   `shim/include/HSUtils.h`, and the `Quantize()` member on
   `shim/include/HemisphereApplet.h`.
3. **HemisphereApplet base-class touch scoped to dep-quant**, not Layer 0a.
   The change is highly dep-specific (adds `Quantize(ch, cv, root, transpose)`
   member redirecting to the `q_engine[]` pool). Layer 0 churn is smaller this
   way; the dep-quant unit test gates the integration. dep-quant's allowed
   surface explicitly includes `shim/include/HemisphereApplet.h` and
   `shim/include/HSUtils.h`.
4. **Time-injection helper API frozen in kickoff**:
   `step_n_inner_ticks(loaded, alg, bus, N)` in
   `harness/tests/applet_test_helpers.{h,cpp}`. File-scope
   `int hem_shim::inner_ticks_override` consumed-and-cleared by `step()`.
   Honors the 10x clocked-multiplier rule.
5. **Layer 0 sub-layered into 0a (everything except clock_m hook) then 0b
   (clock_m hook)**. Both touch the inner-tick loop in `hemispheres_shim.h`;
   combining them in one commit would conflate the helper's correctness proof
   with the clock_m hook's correctness proof. Sequenced.
6. **Vendor unmodified rule applies to all dep ports.** If a vendor header
   does not compile against the shim, the SHIM ADAPTS, not the vendor. Same
   rule as applet ports.

## Cross-dep concerns

### Shared `HemisphereApplet.h` and `HSUtils.h`

dep-quant is the only dep that touches the shared base class. Its allowed
surface explicitly names these files. All other deps (vec-osc, lorenz,
tideslite, clock-mgr, cv-map) have **forbidden** surface covering these two
files. The hook enforces.

### Shared `hemispheres_shim.h` (inner-tick loop)

Layer 0a adds the time-injection helper override-global + consume-and-clear
into the inner-tick loop. Layer 0b adds `clock_m.advance_one_tick()` (or
equivalent surface chosen by dep-clock-mgr) alongside `OC::CORE::ticks += 1`.
**Sequenced**: Layer 0b commits after Layer 0a. Layer 1 implementers (other
than dep-clock-mgr) have `hemispheres_shim.h` as **forbidden** surface.

dep-clock-mgr's allowed surface includes `shim/include/hemispheres_shim.h`
ONLY for the inner-tick hook. Layer 0b actually authors the hook so the
implementer subagent does NOT need to edit hemispheres_shim.h; dep-clock-mgr
implementer ships the ClockManager vendor source + wrapper alone. This
keeps shared-file conflict surface zero for Layer 1.

### Shared `util/util_macros.h`

Layer 0a vendors `util/util_macros.h` verbatim. dep-lorenz and dep-quant
both depend on it (braids_quantizer.h includes it; streams_lorenz_generator.h
includes it). Layer 0a commits it once; both deps consume it. **Forbidden**
in any implementer's surface.

CONSTRAIN is already defined in shim's `util/util_math.h` with identical
body; vendor's util_macros.h guards CONSTRAIN with `#ifndef`. No collision.

### Shared `util/util_math.h`

Layer 0a appends SCALE8_16 and USAT16 to the existing shim header (defensive
addition; deps don't directly use them but Phase 6 applets will). Preserves
existing free function `Proportion`. **Forbidden** in any implementer's
surface.

### Shared `Arduino.h`

Layer 0a appends `millis()` and `elapsedMillis`. **Forbidden** in any
implementer's surface.

### Vendor STL deps on ARM

HSClockManager.h pulls `<functional>` and `<vector>`. The Phazerville vendor
compiles for Teensy 4.1 which uses arm-none-eabi-gcc with full libstdc++; the
NT toolchain is arm-none-eabi-c++ which should support both headers. If `make
arm` fails on STL inclusion, **abort and surface** rather than stripping STL
from vendor source (violates vendor-unmodified rule).

### Vendor SD-card deps

OC_scales.h includes `FS.h` (Teensy file system) for scala file load/save. NT
has no SD card; the methods are dead code on this target. Layer 0a adds a
minimal `shim/include/FS.h` stub providing an empty `File` class so vendor
headers compile. dep-quant unit test never exercises the scala paths.

## Dep-port boundary predicates

1. **Vendor unmodified rule.** dep ports vendor source files verbatim. Shim
   adapts; vendor does not.
2. **Isolated unit test required.** Each dep ships at least one Catch2 test
   in `harness/tests/test_dep_<slug>.cpp` exercising known-input/known-output
   vendor invariants.
3. **Output-parity expectation, two classes:**
   - **Integer-only (byte-identical required across host clang++ x86 and
     arm-none-eabi-c++)**: Lorenz integer math (`SCALE8_16`/`USAT16` paths),
     Quantizer scale lookups (pitch -> scale-index is int), CVInputMap.
   - **Float-using (documented tolerance, typically 1 LSB of final
     int16/int32 output)**: VectorOscillator phase math, tideslite sample
     synthesis, parts of Quantizer pitch math (centihertz/millihertz
     scaling).
   - **Mixed**: ClockManager (tempo math is float, tick advancement is int).
     dep-clock-mgr unit test states which methods fall in which class.

   CI runs host side only. Hardware smoke check in Layer 3 is the practical
   bit-parity test, not the unit test.
4. **No applet ports.** Phase 5 adds no new entry to `applet_indices.h`,
   `HemispheresFactory.h`, or `PhzIcons.h`. The 31 existing applets must
   continue compiling and passing. Pre-commit hook hard-rejects any
   `phase5-dep/*` commit that stages these three files.

## Phase 6 carry-forward inventory

Applets unblocked by Phase 5 deps that Phase 6 audits against the now-extended
shim surface:

- dep-vec-osc unblocks: VectorLFO, VectorEG, VectorMod, VectorMorph, Relabi,
  plus drum/synth applets pending re-audit (Drum1, Drum2, ASR-Drums, etc.).
- dep-lorenz unblocks: LowerRenz.
- dep-tideslite unblocks: EbbAndLfo, WTVCO.
- dep-clock-mgr unblocks: Metronome immediately; ClockSetup variants and
  every clock-aware applet long term.
- dep-quant unblocks: Pigeons, Strum, Shredder, Carpeggio, Squanch,
  Chordinator, DualQuant, EnigmaJr, OffsetQuant, MultiScale, ScaleDuet,
  DuoTET, EnsOscKey, Calibr8.
- dep-cv-map unblocks: Combin8.
- Time-injection helper unblocks: ResetClock (the natural Phase 6 first
  cat-C probe), Shuffle, Xfader, Scope, and any other applet whose tests
  need precise tick-level state evolution.

Phase 6 inherits these disciplines from Phase 5 audit:

- Helper-design as discrete preflight deliverable, not brainstorm output.
- Tighter cat-C demotion abort threshold ("more than 2 of N demote, halt and
  assess whether the boundary is wrong" vs Phase 5's superseded "more than
  3 of 7-9").
- ResetClock is the natural first cat-C applet because it exercises both
  Clock-driven state evolution and `OC::CORE::ticks` advancement.
- Cat-C boundary revision is the expected outcome of a failed audit, not
  phase abort.

## Out of scope (defer to Phase 6+)

- All applet ports. Phase 6 ships the applet inventory.
- Cat-C applet uses of the time-injection helper itself.
- New hemisphere bus features (pair-applet variants, audio side).
- Vendor SDK changes (`vendor/distingNT_API` stays at `cd12d876`).
- Vendor commit pin (`vendor/O_C-Phazerville` stays at `7800d929`).

## Test invariant proposals (one short paragraph per dep)

### dep-vec-osc

Construct a VectorOscillator at a fixed waveform (e.g., the SEGMENTS-table
constant for the canonical sine), set a fixed `SetPhaseIncrement(0x10000)`,
call `Next()` 256 times, and assert the output sequence's first 16 samples
match a pre-computed expected array. Tolerance: 1 LSB on int16 output to
absorb float-rounding divergence between host and ARM. For RelabiManager,
register two hemispheres, write a known value bus pair, read back, assert.

### dep-lorenz

Construct LorenzGeneratorManager. Set freq=128, rho=80 on hem 0. Reset, then
call Process() 1024 times. Assert `GetOut(0)` and `GetOut(1)` first 8 samples
are byte-identical to a pre-computed integer expected array (integer math
only).

### dep-tideslite

Call `ComputePhaseIncrement(C4=0)` and assert against a pre-computed value
(integer math). Call `ProcessSample(slope=0.5, shape=0.5, fold=0.0, phase=0.25,
&out)` and assert `out` is within 1-LSB tolerance of a pre-computed float.
PhaseExtractor: feed a 4-step clock pattern, assert per-tick phase output.

### dep-clock-mgr

Construct ClockManager. `SetTempoBPM(120)`. Advance the clock by `Process()`
N times (or via the hemispheres_shim.h inner-tick hook installed in Layer 0b)
and assert `IsRunning()` toggles, `Tock(0)` rises every (60 * 1000) / 120
units, `tempo` field reads 120. Mixed: tempo math is float; tick assertions
are integer.

### dep-quant

Construct `braids::Quantizer`, configure with chromatic scale
(`OC::Scales::GetScale(SCALE_SEMI)`), process input pitches at known
millivolt values, assert output pitch round-trips bit-identically (integer
math). HS:: glue: `HS::QuantizerConfigure(0, SCALE_SEMI, 0xffff)` then
`HS::Quantize(0, ONE_OCTAVE, 0, 0)` returns the expected scale-locked pitch.
`MIDIQuantizer::NoteNumber(ONE_OCTAVE)` returns 60+12 (octave above middle
C with default bias).

### dep-cv-map

Construct `cvmap[4]` with `SetMap(0, 0)`, write 1.0V to bus channel 0,
assert `cvmap[0].In()` returns 1536 (1.0V in hem units). bjorklund: generate
a Euclidean 3-of-8 pattern, assert against a pre-computed bitmask. clkdivmult:
construct, set div=2, drive 10 input ticks, assert 5 output ticks.

## Layer 0 design (frozen in kickoff, restated here)

### Layer 0a (sequential parent commits)

1. `shim/include/util/util_macros.h`: vendor `vendor/O_C-Phazerville/software/src/util/util_macros.h` verbatim (SWAP, DISALLOW_COPY_AND_ASSIGN, CLIP, CONSTRAIN guarded, ARRAY_SIZE).
2. `shim/include/util/util_math.h`: append SCALE8_16, USAT16, plus any other shared macros from vendor `util/util_math.h` (e.g., USAT8) that may be needed. Preserve existing `Proportion`.
3. `shim/include/Arduino.h`: append `millis()` returning `OC::CORE::ticks / 1000` (vendor pattern; tests don't depend on millis precision), and `elapsedMillis` class wrapping a uint32 baseline against `OC::CORE::ticks`.
4. `shim/include/FS.h`: minimal stub `class File {};` so OC_scales.h compiles. Layer 0 marks methods using `File&` as dead-code paths.
5. Time-injection helper API in `harness/tests/applet_test_helpers.{h,cpp}`: declare and define `step_n_inner_ticks(loaded, alg, bus, N)`. Add file-scope `int hem_shim::inner_ticks_override` and consume-and-clear in `hemispheres_shim.h` `step()`.
6. Time-injection helper unit tests: both (a) and (b) added to `harness/tests/test_hemispheres.cpp` under `=== BEGIN helper ===` / `=== END helper ===` markers.
   - (a) Cumulus equivalence: `step_n_inner_ticks(loaded, alg, bus, 10)` produces state byte-identical to `step_n_frames(loaded, alg, bus, 32)` for Cumulus.
   - (b) Empty + held-gate tick-advancement invariant: `step_n_inner_ticks(loaded, alg, bus, 1000)` advances `OC::CORE::ticks` by exactly 1000, holds `clocked[0]` true across all 1000 inner ticks (via `hold_gate`), decrements `frame.clock_countdown[0]` by exactly 1000 if pre-loaded to 1000.
7. Section markers in `harness/tests/test_hemispheres.cpp`: reserve per-dep regions for Phase 6 applet ports (one `=== BEGIN <dep-slug> ===` / `=== END <dep-slug> ===` block per dep; empty body). Phase 5 itself adds no applet tests.
8. Per-dep test skeletons: `harness/tests/test_dep_<slug>.cpp` for each of the six deps (vec-osc, lorenz, tideslite, clock-mgr, quant, cv-map). Each file is a Catch2 source with one placeholder TEST_CASE that the implementer replaces. Each file added to the host test build via `Makefile` additions.
9. Pre-commit hook updated for `phase5-dep/*` and `dr/phase5-deps-plan` accept-list with hard-reject on `applet_indices.h`/`HemispheresFactory.h`/`PhzIcons.h` for `phase5-dep/*` branches.
10. No HemisphereApplet base-class touches in Layer 0a (per load-bearing decision 3; dep-quant owns this).

### Layer 0b (sequential parent commit, depends on Layer 0a)

1. `shim/include/hemispheres_shim.h`: add `clock_m.advance_one_tick()` (or whichever surface dep-clock-mgr provides) inside the inner-tick loop alongside `OC::CORE::ticks += 1`. Layer 0b authors this; dep-clock-mgr implementer ships the ClockManager source + wrapper alone and does NOT edit hemispheres_shim.h.

## Spec coverage

The spec inherits the Phase 4 recipe section + section markers convention.
New "Vendor dep port recipe" subsection documents the standard shape: vendor
source verbatim under `shim/include/<area>/` (or `shim/src/` for .cpp), one
minimal wrapper header if surface needs adapting, one Catch2 test file at
`harness/tests/test_dep_<slug>.cpp` with known-input/known-output invariants.
