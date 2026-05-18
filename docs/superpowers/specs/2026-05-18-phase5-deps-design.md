# Spec: Phase 5 vendor dep port batch

Date: 2026-05-18
Status: Active
Owner: Dan
Brainstorm: `docs/superpowers/brainstorms/2026-05-18-phase5-deps-brainstorm.md`
Branch: `dr/phase5-deps-plan`
Worktree: `.worktrees/phase5-deps-plan`
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.

## Goal

Land six vendor dep ports plus shared Layer 0 shim infrastructure plus the
time-injection harness helper on `dr/phase5-deps-plan`. No applet ports land
in Phase 5. The 31 existing applets continue to pass at baseline
(155 cases / 1803 assertions).

## Vendor dep port recipe

A dep port has four pieces:

1. **Vendor source under shim**, verbatim. Headers land in
   `shim/include/<area>/<file>.h`, sources in `shim/src/<area>/<file>.cpp`.
   `<area>` is a per-dep subdirectory (e.g. `shim/include/vector_osc/`,
   `shim/include/quant/`, `shim/include/lorenz/`,
   `shim/include/tideslite/`, `shim/include/cv_map/`,
   `shim/include/clock/`). Each `<area>` keeps the dep's vendor files
   isolated from cross-dep collisions. Place the file at the path the vendor
   include directive resolves to: if vendor source `#include
   "util/util_macros.h"`, the shim mirrors that as
   `shim/include/util/util_macros.h` (already shared at Layer 0). If vendor
   source `#include "../util/util_math.h"`, the per-area subdir uses one
   level of nesting so the relative path resolves.
2. **Minimal wrapper header (optional)** at `shim/include/<area>/wrapper.h`.
   Only if the vendor surface requires adapting one specific symbol to shim
   conventions (e.g., reroute vendor `OC::CORE::ticks` reference through
   `OC_core.h`). Most deps need no wrapper; the vendor header compiles
   against the shim as-is.
3. **One Catch2 test file** at `harness/tests/test_dep_<slug>.cpp`. Exercises
   known-input/known-output vendor invariants. Output-parity class
   (integer-only or float-tolerance) stated at the top of the file.
4. **Section markers preserved** in `harness/tests/test_hemispheres.cpp` for
   the Phase 6 applets this dep unblocks. Phase 5 commits empty regions; the
   markers are placeholders only.

The implementer subagent reads vendor source from
`vendor/O_C-Phazerville/software/src/<vendor-path>` and copies the file into
`shim/include/<area>/<vendor-path-leaf>` byte-for-byte. The shim's existing
include search path (`-Ishim/include`) makes the copied file visible to
applet ports that `#include "<vendor-path-leaf>"`. Cross-applet includes
(e.g., one applet `#include`s `HSVectorOscillator.h`) resolve through the
shim include path; applet vendor source paths
(`vendor/O_C-Phazerville/software/src/applets/`) are NOT in the include
path.

### Recipe spot-check (used by spec footer verification)

- **Vendor source verbatim**: `shim/include/quant/braids_quantizer.h` MUST
  be byte-identical to
  `vendor/O_C-Phazerville/software/src/braids_quantizer.h`. `diff` returns
  empty.
- **Output-parity class declared**: `harness/tests/test_dep_quant.cpp`
  states at the top of the file (in a `// Output-parity class: integer-only`
  comment) which parity class the test enforces.
- **Section markers preserved**: For dep-quant, the unblocked applets
  (Pigeons, Strum, Shredder, Carpeggio, Squanch, Chordinator, DualQuant,
  EnigmaJr, OffsetQuant, MultiScale, ScaleDuet, DuoTET, EnsOscKey, Calibr8)
  each have a `// === BEGIN <slug> ===` / `// === END <slug> ===` block in
  `test_hemispheres.cpp` with empty body. Phase 6 fills them.

## Per-dep entries

### dep-vec-osc (bundled VectorOscillator + WaveformManager + RelabiManager)

**Vendor files:**

- `vendor/O_C-Phazerville/software/src/vector_osc/HSVectorOscillator.h` (283 lines)
- `vendor/O_C-Phazerville/software/src/vector_osc/WaveformManager.h` (223 lines)
- `vendor/O_C-Phazerville/software/src/vector_osc/waveform_library.h` (319 lines)
- `vendor/O_C-Phazerville/software/src/HSRelabiManager.h` (88 lines)

**Shim destinations:**

- `shim/include/vector_osc/HSVectorOscillator.h`
- `shim/include/vector_osc/WaveformManager.h`
- `shim/include/vector_osc/waveform_library.h`
- `shim/include/HSRelabiManager.h`

**Surface vendor applets touch:**

- `WaveformManager::VectorOscillatorFromWaveform(int)`,
  `WaveformManager::GetNextWaveform(int)`.
- `VectorOscillator::SetFrequency(centihertz)`,
  `SetPhaseIncrement(uint32)`, `SetScale(int)`, `Offset(int)`, `Reset()`,
  `Reset(phase)`, `Sustain()`, `Cycle()`, `Start()`, `Release()`, `Next()`,
  `TotalTime()`, `GetSegment(int)`, `SegmentCount()`, `GetPhase()`.
- `RelabiManager::Register(int hem)`, `Unload(int hem)`, `IsLinked()`,
  `WriteValues(...)`, `ReadValues(...)`, `WriteGates(...)`, `ReadGates(...)`.

**Status:** Layer 1 implementer task `dep-vec-osc`.

**Test invariant:** Construct a VectorOscillator at the canonical SINE
waveform, set `SetPhaseIncrement(0x10000)`, call `Next()` 256 times. Assert
the first 16 output samples match a pre-computed expected array within 1-LSB
tolerance (float-using class). For RelabiManager, register hem 0 and hem 1,
write a known value pair, read back, assert byte-identical (integer class).

**Shim prereqs (Layer 0):** `util/util_math.h` SCALE8_16/USAT16 additions
(defensive; vec-osc itself does not directly call these but its waveform
math may).

**LoC:** 913 vendored + ~100 shim wrapper.

**Phase 6 unblocks:** VectorLFO, VectorEG, VectorMod, VectorMorph, Relabi,
plus drum/synth applets pending re-audit.

### dep-lorenz (LorenzGeneratorManager + streams_lorenz_generator)

**Vendor files:**

- `vendor/O_C-Phazerville/software/src/HSLorenzGeneratorManager.h` (79 lines)
- `vendor/O_C-Phazerville/software/src/streams_lorenz_generator.h` (140 lines)
- `vendor/O_C-Phazerville/software/src/streams_lorenz_generator.cpp` (231 lines)
- `vendor/O_C-Phazerville/software/src/streams_resources.h` (105 lines)
- `vendor/O_C-Phazerville/software/src/streams_resources.cpp` (1437 lines; mostly
  inert data; only `streams::lut_lorenz_rate[]` at line 1359 is referenced by
  the Lorenz generator)

**Shim destinations:**

- `shim/include/HSLorenzGeneratorManager.h`
- `shim/include/lorenz/streams_lorenz_generator.h`
- `shim/src/lorenz/streams_lorenz_generator.cpp`
- `shim/include/lorenz/streams_resources.h`
- `shim/src/lorenz/streams_resources.cpp`

**Surface vendor applets touch:**

- Singleton `lorenz_m` of `HSLorenzGeneratorManager`.
- `SetFreq(int hem, int freq)`, `SetRho(int hem, int rho)`, `Reset(int hem)`,
  `Process()`, `GetOut(int channel)`.

**Status:** Layer 1 implementer task `dep-lorenz`.

**Test invariant:** Construct LorenzGeneratorManager. Call
`SetFreq(0, 128)`, `SetRho(0, 80)`, `Reset(0)`. Then `Process()` 1024 times.
Assert `GetOut(0)` and `GetOut(1)` first 8 samples byte-identical to a
pre-computed integer expected array (integer-only class).

**Shim prereqs (Layer 0):** `util/util_macros.h` (CONSTRAIN guarded; CLIP;
ARRAY_SIZE) vendored from `vendor/O_C-Phazerville/software/src/util/util_macros.h`.

**LoC:** 1992 vendored + ~50 shim wrapper.

**Phase 6 unblocks:** LowerRenz.

### dep-tideslite (tideslite + PhaseExtractor)

**Vendor files:**

- `vendor/O_C-Phazerville/software/src/tideslite.h` (224 lines)
- `vendor/O_C-Phazerville/software/src/util/util_phase_extractor.h` (78 lines)
- `vendor/O_C-Phazerville/software/src/util/util_pattern_predictor.h` (95 lines;
  transitive dep of util_phase_extractor.h via `#include "util_pattern_predictor.h"`)

**Shim destinations:**

- `shim/include/tideslite/tideslite.h`
- `shim/include/util/util_phase_extractor.h`
- `shim/include/util/util_pattern_predictor.h`

**Surface vendor applets touch:**

- Free functions `ComputePhaseIncrement(int pitch)`,
  `ComputePitch(uint32_t phase_inc)`.
- `ProcessSample(float slope, float shape, float fold, float phase,
  TidesLiteSample& out)`.
- `PhaseExtractor<NumPredictedValues>` template class.

**Status:** Layer 1 implementer task `dep-tideslite`.

**Test invariant:** `ComputePhaseIncrement(0)` returns the pre-computed
phase increment for C4 (integer-only assertion). `ProcessSample(0.5f, 0.5f,
0.0f, 0.25f, out)` produces `out.unipolar` / `out.bipolar` within 1-LSB of a
pre-computed expected (float-tolerance class). PhaseExtractor: feed a
known 4-step clock pattern, assert per-tick phase output is the expected
fraction.

**Shim prereqs (Layer 0):** None beyond `<stdint.h>` (vendor includes only
`<stdint.h>`).

**LoC:** 397 vendored + ~50 shim wrapper.

**Phase 6 unblocks:** EbbAndLfo, WTVCO (audit pending).

### dep-clock-mgr (ClockManager full)

**Vendor files:**

- `vendor/O_C-Phazerville/software/src/HSClockManager.h` (367 lines)
- Subset of `vendor/O_C-Phazerville/software/src/HSMIDI.h` (412 lines; the
  ClockManager pulls HSMIDI for `MIDIQuantizer` and MIDI-clock plumbing.
  dep-clock-mgr extracts MIDIQuantizer into dep-quant's space and vendors
  the rest as needed; the implementer decides via the vendor-unmodified
  rule whether to vendor the full HSMIDI.h or stub the MIDI-IO globals).

**Shim destinations:**

- `shim/include/HSClockManager.h` (replaces current 3-method stub)
- `shim/include/clock/HSMIDI_subset.h` (only if implementer needs the
  MIDI-clock plumbing; otherwise the existing shim provides empty MIDI-IO
  globals).

**Surface vendor applets touch:**

- State queries: `IsRunning()`, `Tock(int ch)`, `Cycle(int hem)`,
  `EndOfBeat(int hem)`.
- Public fields: `tempo`, `shuffle`.
- Control: `SetTempoBPM(int bpm)`, `GetTempo()`,
  `SetMultiply(int steps, int ch)`, `GetMultiply(int ch)`,
  `Modulate(int tempo_cv, int swing_cv)`.

**Status:** Layer 1 implementer task `dep-clock-mgr`. Layer 0b separately
authors the inner-tick hook in `shim/include/hemispheres_shim.h`.

**Test invariant:** Construct ClockManager via the existing `extern clock_m`
global. `SetTempoBPM(120)` then call `advance_one_tick()` (or whatever
tick-advance surface the vendor provides) N times equivalent to one beat at
120 BPM. Assert `IsRunning()` reads true, `Tock(0)` rises at expected tick
boundary, `GetTempo()` returns 120 (mixed class: tempo float, ticks
integer).

**Shim prereqs (Layer 0):** `Arduino.h` `millis()` and `elapsedMillis`
(vendor uses both). `<functional>` and `<vector>` via libstdc++; verified to
work on arm-none-eabi-c++ during Layer 1 build.

**LoC:** 367 vendored + (0 or 412 for HSMIDI subset) + ~50 shim wrapper.

**Phase 6 unblocks:** Metronome immediately; ClockSetup variants and every
clock-aware applet long term.

### dep-quant (Quantizer subsystem, MONOLITHIC)

**Vendor files:**

- `vendor/O_C-Phazerville/software/src/braids_quantizer.h` (123 lines)
- `vendor/O_C-Phazerville/software/src/braids_quantizer_scales.h` (353 lines)
- `vendor/O_C-Phazerville/software/src/OC_scales.h` (73 lines)
- `vendor/O_C-Phazerville/software/src/OC_scales.cpp` (431 lines)
- Extracted `MIDIQuantizer` class from
  `vendor/O_C-Phazerville/software/src/HSMIDI.h` lines 385-411 (~30 lines).

**Shim destinations:**

- `shim/include/quant/braids_quantizer.h`
- `shim/include/quant/braids_quantizer_scales.h`
- `shim/include/quant/OC_scales.h`
- `shim/src/quant/OC_scales.cpp`
- `shim/include/quant/MIDIQuantizer.h` (vendor extract)
- `shim/include/HSUtils.h` (modified: add HS:: quantizer surface; see below)
- `shim/include/HemisphereApplet.h` (modified: add `Quantize(ch, cv, root,
  transpose)` member redirecting to `HS::Quantize`)
- `shim/src/quant/q_engine.cpp` (the `HS::q_engine[]` global storage)

**Surface vendor applets touch:**

- `braids::Quantizer::Init()`, `Configure(const Scale&, uint16_t mask)`,
  `Process(int32_t pitch, int32_t root, int32_t transpose)`,
  `Lookup(int note)`.
- `HS::QuantEngine` struct (vendor `HSUtils.h:128-184`) with `quantizer`,
  `scale`, `mask`, `root_note`, `octave` and methods
  `Reconfig`, `Configure`, `EditMask`, `NudgeScale`, `RotateMask`, `Process`,
  `Lookup`, `Size`.
- `HS::Quantize(int ch, int cv, int root = 0, int transpose = 0)`,
  `QuantizerLookup`, `QuantizerConfigure`, `GetScale`, `SetScale`,
  `GetRootNote`, `SetRootNote`, `NudgeRootNote`, `NudgeOctave`,
  `NudgeScale`, `QuantizerEdit` (vendor `HSUtils.h:225-235`).
- `HS::QUANT_CHANNEL_COUNT` (enum value = 8; vendor `HSUtils.h:107`).
- `HS::q_engine[QUANT_CHANNEL_COUNT]` global pool.
- `HS::GetLatestNoteNumber(int ch)` reading the latest note from the
  per-channel quantizer.
- `OC::Scales::GetScale(int index)`, `OC::Scales::NUM_SCALES`,
  `OC::Scales::SCALE_SEMI` (vendor `OC_scales.h:14-30`).
- `OC::SemitoneQuantizer` (vendor in `OC_DigitalInputs.h` or
  `OC_DAC.h`; check vendor source for exact location).
- `OC::DAC::kOctaveZero` (vendor `OC_DAC.h`; the shim's `OC_DAC.h` may need
  this constant added).
- `MIDIQuantizer::NoteNumber(int cv, int transpose = 0,
  uint8_t bias = OC::DAC::kOctaveZero)`, `MIDIQuantizer::CV(uint8_t
  note_number, int transpose = 0, uint8_t bias = OC::DAC::kOctaveZero)`.

**Status:** Layer 1.5 implementer task `dep-quant`. May run concurrently
with Layer 1 (no shared file outside the quant area, except the
HemisphereApplet base and HSUtils which dep-quant alone touches).

**Test invariant:** Construct `braids::Quantizer`, configure with
`OC::Scales::GetScale(OC::Scales::SCALE_SEMI)`. Process input pitches at
ONE_OCTAVE (= 1536) increments; assert chromatic scale round-trip is
byte-identical (integer-only). `HS::QuantizerConfigure(0, SCALE_SEMI,
0xffff)` then `HS::Quantize(0, ONE_OCTAVE, 0, 0)` returns the expected
scale-locked pitch. `MIDIQuantizer::NoteNumber(ONE_OCTAVE)` returns the
midi note one octave above the kOctaveZero bias note.

**Shim prereqs (Layer 0):** `util/util_macros.h` (braids_quantizer.h
`#include`s it). `Arduino.h` (OC_scales.h `#include`s it). `FS.h` minimal
stub `class File {};` added in Layer 0a so OC_scales.h compiles. The
`OC::DAC::kOctaveZero` constant added to existing
`shim/include/OC_DAC.h` if not already present (verify in Layer 0a; if
missing add as a parent-layer change, not dep-quant change).

**LoC:** 1010 vendored, plus ~250 shim glue (HS:: surface, `q_engine[]`
storage, base-class redirect).

**Phase 6 unblocks:** Pigeons, Strum, Shredder, Carpeggio, Squanch,
Chordinator, DualQuant, EnigmaJr, OffsetQuant, MultiScale, ScaleDuet,
DuoTET, EnsOscKey, Calibr8.

### dep-cv-map (CVInputMap full + bjorklund + clkdivmult)

**Vendor files:**

- `vendor/O_C-Phazerville/software/src/CVInputMap.h` (266 lines)
- `vendor/O_C-Phazerville/software/src/bjorklund.h` (54 lines)
- `vendor/O_C-Phazerville/software/src/bjorklund.cpp` (1347 lines; pure
  data table)
- `vendor/O_C-Phazerville/software/src/util/clkdivmult.h` (131 lines)

**Shim destinations:**

- `shim/include/CVInputMap.h` (replaces current 20-LoC stub)
- `shim/include/cv_map/bjorklund.h`
- `shim/src/cv_map/bjorklund.cpp`
- `shim/include/util/clkdivmult.h`

**Surface vendor applets touch:**

- `cvmap[]` array (4 entries) with `In()`, `SetMap(int slot, int channel)`,
  `GetMap(int slot)`, methods documented in vendor `CVInputMap.h`.
- `trigmap[]` array with `Gate()` and equivalent map methods.
- `bjorklund_patterns[]` lookup table (used for Euclidean pattern
  generation in Combin8).
- `util::ClockDivider`, `util::ClockMultiplier` templates from clkdivmult.h
  (audit vendor surface for exact API).

**Status:** Layer 1 implementer task `dep-cv-map`.

**Test invariant:** Construct `cvmap[0].SetMap(0, 0)`, write 1.0V to bus
channel 0, assert `cvmap[0].In()` returns 1536 (integer-only). bjorklund:
generate the Euclidean 3-of-8 pattern, assert bitmask is exactly 0b10010010
(or whatever the vendor table emits; pin to the actual vendor pattern,
byte-identical). clkdivmult: construct `util::ClockDivider`, set divisor to
2, feed 10 ticks, assert 5 output ticks (integer-only).

**Shim prereqs (Layer 0):** None beyond existing `HSIOFrame.h`.

**Surface compatibility note:** The existing 20-LoC shim stub of
`CVInputMap.h` is referenced by `HemisphereApplet.h:14` and used by
`HemisphereApplet::In(ch)` at line 52 (`cvmap[ch + channel_offset()].In()`)
and `Gate(ch)` at line 61 (`trigmap[ch + channel_offset()].Gate()`). The
full vendor CVInputMap MUST preserve the `In()` and `Gate()` behavior the
shim depends on, or `HemisphereApplet.h` breaks. dep-cv-map's allowed
surface explicitly includes a no-op edit to `HemisphereApplet.h` ONLY if
the vendor surface requires API changes at the call sites. Otherwise the
file is forbidden.

**LoC:** 1798 vendored + ~30 shim wrapper.

**Phase 6 unblocks:** Combin8.

## Layer 0 entries

### Layer 0a additions (sequential parent commits)

1. **`shim/include/util/util_macros.h`**: vendor
   `vendor/O_C-Phazerville/software/src/util/util_macros.h` lines 1-21
   verbatim. SWAP, DISALLOW_COPY_AND_ASSIGN, CLIP, CONSTRAIN (guarded by
   `#ifndef CONSTRAIN`; identical body to `shim/include/util/util_math.h:6-8`
   so no collision), ARRAY_SIZE.
2. **`shim/include/util/util_math.h`**: append SCALE8_16 (vendor
   `util_math.h:164`) and USAT16 from vendor source. Preserve existing
   `Proportion` free function at line 16-18.
3. **`shim/include/Arduino.h`**: append `inline uint32_t millis() { return
   OC::CORE::ticks / 1000; }` after existing `micros()` at line 29. Append
   `class elapsedMillis` with constructor capturing `millis()` baseline,
   `operator uint32_t() const { return millis() - base; }`,
   `operator=(uint32_t)` to reset baseline.
4. **`shim/include/FS.h`**: new file. Minimal stub `class File {}; class FS
   {};` providing types so vendor `OC_scales.h` compiles. Methods that take
   `File&` (`SaveToScala`, `LoadScala`) are dead-code paths on NT (no SD).
5. **`shim/include/OC_DAC.h`**: audit for `kOctaveZero` constant; add if
   missing. Vendor `OC_DAC.h` defines it as `static constexpr uint8_t
   kOctaveZero = 5` (octave bias for note number conversion).
6. **`harness/tests/applet_test_helpers.h`** + **`.cpp`**: declare and
   define `int step_n_inner_ticks(nt::LoadedPlugin* loaded, _NT_algorithm*
   alg, float* bus, int N)`. Implementation calls `hem_shim` step() with
   N inner ticks per step via the override-global mechanism.
7. **`shim/include/hemispheres_shim.h:171`**: insert override-global
   consumption before existing `int ticks_this_step = numFrames / 3;`. Add
   file-scope `int inner_ticks_override = 0` inside `namespace hem_shim`
   (declared in header, defined once in `shim/src/globals.cpp`). The step()
   prologue reads:

   ```cpp
   int ticks_this_step;
   if (hem_shim::inner_ticks_override > 0) {
       ticks_this_step = hem_shim::inner_ticks_override;
       hem_shim::inner_ticks_override = 0;
   } else {
       ticks_this_step = numFrames / 3;
       if (ticks_this_step < 1) ticks_this_step = 1;
   }
   ```

   Gate writes (`HS::frame.clocked[]` / `gate_high[]` lines 160-167) and CV
   writes (lines 142-145) remain at step() prologue, run ONCE, are held
   across all N inner ticks. This preserves the existing 10x clocked rule
   for both override and default paths.
8. **`harness/tests/test_hemispheres.cpp`**: add `// === BEGIN helper ===`
   / `// === END helper ===` block with two TEST_CASEs:
   - (a) `step_n_inner_ticks` equivalence with `step_n_frames`: load
     Cumulus, set known params, run `step_n_inner_ticks(.., 10)` from
     state S0 producing S1; reset, run `step_n_frames(.., 32)` from S0
     producing S2; assert `S1.OnDataRequest() == S2.OnDataRequest()` plus
     selected bus output bytes byte-identical.
   - (b) Empty + held-gate tick-advancement invariant: load Empty applet,
     `hold_gate(bus, LEFT, 0, ...)` to drive gate-high across the whole
     buffer, pre-load `HS::frame.clock_countdown[0] = 1000`, capture
     `OC::CORE::ticks` baseline T0, run `step_n_inner_ticks(.., 1000)`,
     assert `OC::CORE::ticks - T0 == 1000`, assert
     `HS::frame.clock_countdown[0] == 0`, assert `HS::frame.clocked[0] ==
     true` (held across all 1000 inner ticks).
9. **`harness/tests/test_hemispheres.cpp`**: section-marker blocks
   (`// === BEGIN <slug> === / // === END <slug> ===`) for each Phase 6
   applet unblocked by Phase 5 deps (see brainstorm's Phase 6 inventory
   table). Phase 5 commits empty bodies.
10. **`harness/tests/test_dep_<slug>.cpp`** for six deps: `test_dep_vec_osc.cpp`,
    `test_dep_lorenz.cpp`, `test_dep_tideslite.cpp`,
    `test_dep_clock_mgr.cpp`, `test_dep_quant.cpp`, `test_dep_cv_map.cpp`.
    Each file is a Catch2 source with one placeholder `TEST_CASE("dep-<slug>
    placeholder", "[dep-<slug>]") { CHECK(true); }`. The implementer replaces
    the placeholder with the real invariant tests.
11. **`Makefile`**: add `test_dep_<slug>.cpp` files to the host test build
    target (current `make test-applets` target). Each dep test is a
    standalone Catch2 binary `build/host/test_dep_<slug>`, or added to the
    existing `test_hemispheres` binary; implementer-by-implementer decision
    (the cleaner pattern is per-dep binary so each can run in isolation).
12. **`.git/hooks/pre-commit`** (in the parent worktree's git common dir):
    update the Phase 4 hook template with `phase5-dep/*` and
    `dr/phase5-deps-plan` accept-list. Hard-reject any `phase5-dep/*`
    commit that stages `shim/include/applet_indices.h`,
    `shim/include/HemispheresFactory.h`, `shim/include/PhzIcons.h` (the
    three integration-owned files). Verify the hook actually rejects by
    staging a forbidden file in a sandbox commit attempt.

### Layer 0b additions (sequential parent commit, after Layer 0a)

1. **`shim/include/hemispheres_shim.h:172`**: inside the existing inner-tick
    loop (the `for (int i = 0; i < ticks_this_step; ++i)` block), add
    `clock_m.advance_one_tick();` (or whichever tick-advance surface the
    dep-clock-mgr port exposes) immediately after `OC::CORE::ticks += 1;`.
    The order matters: `OC::CORE::ticks` advances first so `clock_m`'s tick
    handler can read it.

## Verification footer

### Spec footer: Recipe spot-check

Three claims about the recipe section. Reviewer verifies each:

1. **Claim**: dep ports vendor source files verbatim, placed at the path
   their `#include` directive resolves to.
   **Verify**: `diff vendor/O_C-Phazerville/software/src/util/util_macros.h
   shim/include/util/util_macros.h` returns empty after Layer 0a commit.
2. **Claim**: each dep has exactly one Catch2 test file at
   `harness/tests/test_dep_<slug>.cpp`.
   **Verify**: `ls harness/tests/test_dep_*.cpp | wc -l` returns 6 after
   Layer 0a commit.
3. **Claim**: section markers preserved in `test_hemispheres.cpp` reserve
   Phase 6 applet test regions.
   **Verify**: `grep -c "=== BEGIN " harness/tests/test_hemispheres.cpp`
   returns at least 1 + (count of Phase 6-unblocked applets) after Layer 0a.

### Spec footer: Per-dep verification

Three deps spot-checked. If more than 1 of 3 wrong, audit every entry.

1. **dep-lorenz vendor file count.**
   **Claim**: 5 vendor files (HSLorenzGeneratorManager.h,
   streams_lorenz_generator.{h,cpp}, streams_resources.{h,cpp}).
   **Verify**: per-dep entry above lists exactly 5 vendor file lines under
   "Vendor files". Yes.
2. **dep-quant LoC.**
   **Claim**: 1010 vendored.
   **Verify**: 123 (braids_quantizer.h) plus 353
   (braids_quantizer_scales.h) plus 73 (OC_scales.h) plus 431
   (OC_scales.cpp) plus 30 (MIDIQuantizer extract) equals 1010. Yes.
3. **dep-clock-mgr Phase 6 unblocks.**
   **Claim**: Metronome immediately; ClockSetup variants and every
   clock-aware applet long term.
   **Verify**: per-dep entry above states this in the "Phase 6 unblocks"
   line. Yes.

### Spec footer: Shared prereq verification

Every Layer 0 addition verified against the per-dep prereq lines:

- `util/util_macros.h` (Layer 0a #1): required by dep-lorenz (CONSTRAIN,
  CLIP, ARRAY_SIZE), dep-quant (braids_quantizer.h `#include`s it). Both
  per-dep entries list this as a shim prereq. Match.
- `util/util_math.h` SCALE8_16/USAT16 (Layer 0a #2): defensive; per-dep
  entries note dep-vec-osc as the closest consumer. Match.
- `Arduino.h` millis/elapsedMillis (Layer 0a #3): required by
  dep-clock-mgr (per-dep entry says so). Match.
- `FS.h` stub (Layer 0a #4): required by dep-quant (OC_scales.h includes
  FS.h). dep-quant per-dep entry lists this. Match.
- `OC_DAC.h` kOctaveZero (Layer 0a #5): required by dep-quant
  (MIDIQuantizer.h uses it). dep-quant per-dep entry lists this. Match.
- Time-injection helper (Layer 0a #6-8): independent infrastructure for
  Phase 6 cat-C applet ports. No Phase 5 dep depends on it. Layer 0a
  unit tests (a) and (b) gate Layer 0a completion.
- Section markers + dep test skeletons (Layer 0a #9-11): infrastructure for
  Phase 5 Layer 1 implementers and Phase 6 applet ports. All six per-dep
  entries reference `harness/tests/test_dep_<slug>.cpp` as the test file.
  Match.
- Pre-commit hook (Layer 0a #12): operational enforcement of the
  no-applet-port invariant. All six per-dep entries have allowed surface
  bounded by hook-enforced rules. Match.
- ClockManager step() prologue hook (Layer 0b #1): required by
  dep-clock-mgr (per-dep entry says Layer 0b separately authors the
  inner-tick hook). Match.

All shared prereqs traced to at least one per-dep consumer or independent
Phase 6 use. No orphan additions.

## Out of scope

- All applet ports. Phase 6 ships the applet inventory.
- Cat-C applet uses of the time-injection helper itself.
- New hemisphere bus features (pair-applet variants, audio side).
- Vendor SDK changes (`vendor/distingNT_API` stays at `cd12d876`).
- Vendor commit pin (`vendor/O_C-Phazerville` stays at `7800d929`).
