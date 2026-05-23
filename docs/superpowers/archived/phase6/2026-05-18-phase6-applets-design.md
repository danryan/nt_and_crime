# Spec: Phase 6 Hemisphere applet port batch

Date: 2026-05-18
Status: Active
Owner: Dan
Brainstorm: `docs/superpowers/brainstorms/2026-05-18-phase6-applets-brainstorm.md`
Branch: `dr/phase6-applets-plan`
Worktree: `.worktrees/phase6-applets-plan`
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.

## Goal

Land 25 Hemisphere applet ports against the Phase 5 dep surface in a single Layer 1 parallel fan-out. Each applet ships with Catch2 host tests, a `pack_<applet>` helper where `OnDataRequest` is non-zero, and a section marker pair in `harness/tests/test_hemispheres.cpp`. No Phase 5 dep code or compiler-rt files change. The 31 existing applets remain green at `make test-applets` (157 cases / 1816 assertions baseline preserved; Phase 6 adds new cases).

## Recipe sections (inherited from kickoff)

The kickoff at `docs/superpowers/prompts/2026-05-18-phase6.md` froze four recipe classes. Per-applet entries below reference the recipe class without redefining it. The recipe sections are reproduced here verbatim for spec self-containment.

### Recipe: Vendor-dep applets (Class A)

Applets that exercise a Phase 5 dep surface (VectorOscillator, Lorenz, CVInputMap). Test pattern:

- Round-trip test: pack/unpack via `pack_<applet>` helper proves `OnDataRequest` bit layout is correct.
- Setup-state test: `Start()` defaults match vendor; `Controller()` observables after a few `step()` calls match expected for known inputs.
- Dep-surface test: at least one assertion proving the applet actually uses its dep correctly. For vec-osc applets, sample a `VectorOscillator` output and verify it falls in the expected range for a known waveform + phase increment. For Lorenz, verify the integrator produces non-constant output.
- Tolerance class: float-using deps use the documented tolerance from Phase 5's per-dep classification (typically 1 LSB of integer-converted output).

Tests use `step_n_frames(loaded, alg, bus, N)` with N at most 32 unless the applet has clock-driven state evolution.

### Recipe: Quantizer applets (Class B)

Applets that exercise the `HS::Quantize` pool on `HemisphereApplet` (the Phase 5 dep-quant base-class touch). Test pattern:

- Round-trip test: pack/unpack helper for the applet's state, including scale index and root note where stored.
- Quantizer-init test: verify the applet's `Start()` initializes the quantizer pool slot for its hemisphere (vendor sets scale via `HS::SetScale(hem, scale_index)`; the test confirms the slot has the expected scale after `Start()`).
- Quantization test: drive a known unquantized pitch through the applet and assert the output is the closest quantized note in the applet's scale. Chromatic scale (scale 0) is the simplest probe.
- Scale-change test: if the applet exposes scale as a parameter, change scale and verify subsequent quantization tracks the new scale.

Tests reference scales by name (e.g., "Chromatic", "Major") not by numeric index, since indices may shift if the table is reordered.

### Recipe: Helper-using cat-C applets (Class C)

Applets whose state evolution depends on `OC::CORE::ticks` advancing across many inner ticks. Test pattern uses `step_n_inner_ticks(loaded, alg, bus, N)` from the Phase 5 helper.

- Round-trip test: pack/unpack helper (standard pattern).
- Tick-budget test: pick N appropriate to the applet's clock-driven semantics. For ResetClock, N is one full reset cycle at the slowest mode (1000-10000 inner ticks; choose based on vendor `RC_TICKS_PER_MS` math). For Shuffle, Xfader, Scope, choose N to traverse one full state cycle.
- Pair the tick-budget test with a short-N control test (N = 10-32) to confirm short-N behavior matches what `step_n_frames` would produce. Catches the failure mode where helper at large N drifts from helper at small N.
- Assertions on `OC::CORE::ticks` delta: confirm the helper advanced ticks by exactly N.
- 10x clocked-multiplier rule still applies: bus-level fire-count assertions against the multiplied Clock(ch) are unreliable. Use round-trip + state-injection + per-inner-tick channels sampled directly.

ResetClock is the natural first Class C probe. Spec entry below cites all five facts from the Phase 4 abort report.

### Recipe: Clock-mgr immediate applets (Class D)

Applets that exercise the full `ClockManager` surface shipped in Phase 5's dep-clock-mgr (BPM, multiply, swing, Tock, Cycle, EndOfBeat). Currently just Metronome.

- Round-trip test (standard pattern).
- ClockManager-state test: confirm `clock_m.SetTempoBPM(120)` followed by a known tick advance produces the expected Tock(ch) sequence.
- Pair with a tick-budget test using `step_n_inner_ticks` if the applet's state evolves over many ticks.

## Per-applet entries

Each entry: class, recipe class, vendor path with line range, status, `OnDataRequest` bit layout summary with `file:line` citation, `Start()` defaults with `file:line` citation, `Controller()` observables with `file:line` citation, test concerns (recipe-class reference + 10x multiplier handling for Class C/D), shim prereqs, closest analogue.

All citations reference `vendor/O_C-Phazerville/software/src/applets/<Applet>.h` at SHA `7800d929`.

### Class A entries

#### VectorLFO

Class A, recipe Class A. Vendor: `applets/VectorLFO.h:1-246`. Status: in scope.

`OnDataRequest` (`VectorLFO.h:141`): packed 6+8+1+5+5+1+5+5+1+5 bits across two channels (waveform/scale/EOC + per-channel freq/scale/EOC), full bit map mirrored in `pack_VectorLFO`. `Start()` (`VectorLFO.h:33`): initializes two `VectorOscillator` instances via `WaveformManager::VectorOscillatorFromWaveform`, default waveform = `HS::Sine`, scale = 32767, freq = default centihertz. `Controller()` (`VectorLFO.h:43`): per-tick advance via `osc[ch].Next()`, output to `Out(ch, value)`. Sets EOC flag at cycle wrap.

Test concerns: round-trip via `pack_VectorLFO`; Start() yields non-zero `osc.SegmentCount()`; Controller produces bounded output for Sine + default freq within 1 LSB tolerance (vec-osc float class).

Shim prereqs: dep-vec-osc (`shim/include/vector_osc/HSVectorOscillator.h`, `WaveformManager.h`, `vec_osc_prereqs.h`). Pre-include `vec_osc_prereqs.h` is at Layer 2 (HemispheresFactory.h).

Analogue: dep-vec-osc test patterns from `harness/tests/test_dep_vec_osc.cpp`.

#### VectorEG

Class A, recipe Class A. Vendor: `applets/VectorEG.h:1-234`. Status: in scope.

`OnDataRequest` (`VectorEG.h:115`): packed waveform per channel + retrigger state + EOC flags. `Start()` (`VectorEG.h:37`): instantiates two envelope-mode `VectorOscillator` instances with default ADSR-shaped waveforms. `Controller()` (`VectorEG.h:47`): gate-driven retrigger on Clock(ch); during release, applies `Release()` semantics; output via `Out`. Sets `eoc[ch]` at release end.

Test concerns: round-trip; Start state matches vendor (released, no output); Clock(ch) retriggers envelope; release transitions correctly.

Shim prereqs: dep-vec-osc.

Analogue: vec-osc dep test.

#### VectorMod

Class A, recipe Class A. Vendor: `applets/VectorMod.h:1-176`. Status: in scope.

`OnDataRequest` (`VectorMod.h:79`): packed per-channel modulation depth + waveform + freq. `Start()` (`VectorMod.h:32`): default modulation depth, waveform = Sine. `Controller()` (`VectorMod.h:41`): modulates CV input by oscillator output.

Test concerns: round-trip; Out(ch) tracks In(ch) when depth=0; modulation depth scales properly; freq affects output rate.

Shim prereqs: dep-vec-osc.

Analogue: VectorLFO.

#### VectorMorph

Class A, recipe Class A. Vendor: `applets/VectorMorph.h:1-179`. Status: in scope.

`OnDataRequest` (`VectorMorph.h:83`): packed morph position + freq + waveform indices for source/target. `Start()` (`VectorMorph.h:32`): defaults to Sine -> Triangle morph at position 0. `Controller()` (`VectorMorph.h:42`): blends two oscillator outputs by morph position; CV input modulates morph.

Test concerns: round-trip; morph=0 yields source waveform output; morph=255 yields target waveform output; intermediate values produce blend.

Shim prereqs: dep-vec-osc.

Analogue: VectorLFO.

#### Relabi

Class A, recipe Class A. Vendor: `applets/Relabi.h:1-579` (largest Phase 6 applet). Status: in scope.

`OnDataRequest` (`Relabi.h:420`): packed across two channels, complex bit layout with per-channel freq, mode, mod indices. Pack helper mirrors byte-by-byte. `Start()` (`Relabi.h:51`): registers with `RelabiManager` singleton via `Register(hem)`. `Controller()` (`Relabi.h:82`): drives Relabi oscillator network through the manager; reads modulation values from cross-hemisphere `ReadValues`.

Test concerns: round-trip with all 64 bits exercised; Start() registers in `RelabiManager` (IsLinked goes true); Controller advances oscillator state; deregister on destruction (vendor `~Relabi()` calls `Unload(hem)`).

Shim prereqs: dep-vec-osc (RelabiManager bundled in dep-vec-osc per Phase 5).

Analogue: dep-vec-osc test patterns.

#### LowerRenz

Class A, recipe Class A. Vendor: `applets/LowerRenz.h:1-134`. Status: in scope.

`OnDataRequest` (`LowerRenz.h:82`): packed rate + range per channel. `Start()` (`LowerRenz.h:36`): instantiates Lorenz generator via singleton; default rate and seed. `Controller()` (`LowerRenz.h:41`): advances Lorenz integrator; outputs X and Y components scaled to Out(0) and Out(1).

Test concerns: round-trip; Start() yields non-constant Controller output (Lorenz seed is non-zero); Controller after N=10 step()s shows two distinct output values; values fall within +/-HEMISPHERE_MAX_CV range.

Shim prereqs: dep-lorenz (`shim/include/lorenz/streams_lorenz_generator.h`, `streams_resources.h`, `HSLorenzGeneratorManager.h`).

Analogue: dep-lorenz test patterns from `harness/tests/test_dep_lorenz.cpp`.

#### Combin8

Class A, recipe Class A. Vendor: `applets/Combin8.h:1-155`. Status: in scope (with Layer 2 base class addition).

`OnDataRequest` (`Combin8.h:85`): packed CVInputMap source indices for two outputs (2x2 matrix of sources). `Start()` (`Combin8.h:39`): default source assignments via `CVInputMap` defaults. `Controller()` (`Combin8.h:41`): each output blends two CV map sources per the matrix.

Test concerns: round-trip; default source assignment yields zero output for zero input; non-default source assignment routes In(ch) to Out(ch) appropriately.

Shim prereqs: dep-cv-map (`shim/include/CVInputMap.h` from Phase 5 dep-cv-map). Plus Layer 2 addition: `gfxDisplayInputMapEditor()` member on `HemisphereApplet.h` (delegates to `cvmap` global, mirrors vendor `HemisphereApplet.h:595`).

Analogue: dep-cv-map test patterns.

### Class B entries

All Class B applets use `HS::Quantize` (vendor base `HemisphereApplet::Quantize`) per Phase 5's dep-quant shim. Recipe Class B applies uniformly. Per-applet entries cover specifics only.

#### Pigeons

Class B, recipe Class B. Vendor: `applets/Pigeons.h:1-201`. Status: in scope.

`OnDataRequest` (`Pigeons.h:105`): packed pigeon positions + scale index. `Start()` (`Pigeons.h:42`): default scale = Chromatic, default pigeon distribution. `Controller()` (`Pigeons.h:49`): on Clock(0), advances pigeons; on Clock(1), reseeds; outputs quantized pitch.

Test concerns: round-trip (mind `SINGING_PIGEON_ICON` Layer 2 stub at `Pigeons.h:39`); Start initializes scale slot; Clock(0) advances state; Out(0) is quantized to current scale.

Shim prereqs: dep-quant. Layer 2 icon stub: `singing_pigeon`.

Analogue: existing quantizer-aware applet patterns from Phase 4.

#### Strum

Class B, recipe Class B. Vendor: `applets/Strum.h:1-276`. Status: in scope.

`OnDataRequest` (`Strum.h:207`): packed chord + strum speed + scale + per-note state. `Start()` (`Strum.h:26`): default chord, default scale. `Controller()` (`Strum.h:34`): on Clock(0), triggers strum; per-tick, advances strum sequencer; outputs quantized chord notes sequentially.

Test concerns: round-trip; Start initializes default chord; strum advances per inner tick; outputs match expected chord notes.

Shim prereqs: dep-quant. Layer 2 icon stub: `strum`.

Analogue: existing quantizer applets.

#### Shredder

Class B, recipe Class B. Vendor: `applets/Shredder.h:1-349`. Status: in scope.

`OnDataRequest` (`Shredder.h:167`): packed sequence + scale + length. `Start()` (`Shredder.h:42`): default sequence, scale = Chromatic. `Controller()` (`Shredder.h:63`): on Clock(0), advances sequence position; outputs quantized pitch + gate.

Test concerns: round-trip across full 64-bit state; sequence advance per Clock(0); quantized output matches scale; Reset on Clock(1).

Shim prereqs: dep-quant. Layer 2 icon stub: `shredder`.

Analogue: existing sequencer-style applets.

#### Carpeggio

Class B, recipe Class B. Vendor: `applets/Carpeggio.h:1-268`. Status: in scope.

`OnDataRequest` (`Carpeggio.h:142`): packed chord + arpeggiator state + scale. `Start()` (`Carpeggio.h:42`): default chord and arp mode. `Controller()` (`Carpeggio.h:51`): on Clock(0), advances arpeggiator; uses `hem_arp_chord.h` vendor helper for chord lookups; outputs quantized arpeggio notes.

Test concerns: round-trip; Start initializes default arp state; Clock(0) advances arp position; Out(0) cycles through chord notes; CV input modulates chord.

Shim prereqs: dep-quant; `hem_arp_chord.h` resolves via vendor relative include from applet path (no shim copy needed; vendor file is a free function library and compiles in the applet TU). Layer 2 icon stub: `carpeggio`.

Analogue: Strum (vendor relative `hem_arp_chord.h`).

#### Squanch

Class B, recipe Class B. Vendor: `applets/Squanch.h:1-210`. Status: in scope.

`OnDataRequest` (`Squanch.h:108`): packed scale + range + per-channel state. `Start()` (`Squanch.h:38`): defaults. `Controller()` (`Squanch.h:41`): quantizes In(ch) to scale; outputs quantized pitch per channel.

Test concerns: round-trip; identity test (input = quantized output for in-scale pitches); scale change reflects in subsequent Out().

Shim prereqs: dep-quant. Layer 2 icon stub: `squanch`.

Analogue: DualQuant.

#### Chordinator

Class B, recipe Class B. Vendor: `applets/Chordinator.h:1-187`. Status: in scope.

`OnDataRequest` (`Chordinator.h:120`): packed chord root + chord shape + scale + voicing. `Start()` (`Chordinator.h:28`): default chord. `Controller()` (`Chordinator.h:35`): outputs the chord notes across two output channels as a polyphonic-style chord.

Test concerns: round-trip; output reflects chord shape; CV root modulation works.

Shim prereqs: dep-quant. Layer 2 icon stub: `chordinate`.

Analogue: Strum (chord-style).

#### DualQuant

Class B, recipe Class B. Vendor: `applets/DualQuant.h:1-142`. Status: in scope (simplest Class B; the canonical quantizer-recipe spot-check applet).

`OnDataRequest` (`DualQuant.h:80`): packed two scale indices + two transpose values. `Start()` (`DualQuant.h:31`): defaults to Chromatic on both channels. `Controller()` (`DualQuant.h:40`): per-channel `Quantize(ch, In(ch), 0, transpose[ch])`; outputs result.

Test concerns: round-trip; default Chromatic identity (In and Out within 1 semitone); scale change to Major drops non-Major input pitches to nearest Major note; transpose offsets output correctly.

Shim prereqs: dep-quant. Layer 2 icon stub: `dualQuantizer`.

Analogue: existing quantizer base class methods.

#### EnigmaJr

Class B, recipe Class B. Vendor: `applets/EnigmaJr.h:1-207`. Status: in scope (with Layer 2 DMAMEM accessibility).

`OnDataRequest` (`EnigmaJr.h:98`): packed TM register state + scale + length. `Start()` (`EnigmaJr.h:37`): instantiates `TuringMachine` with default seed. `Controller()` (`EnigmaJr.h:50`): on Clock(0), advances TM; quantizes TM output via `Quantize`.

Test concerns: round-trip; Start initializes deterministic state; Clock(0) advances TM register; output is quantized to scale.

Shim prereqs: dep-quant; vendor `enigma/TuringMachine.h`, `enigma/TuringMachineState.h`, `enigma/EnigmaOutput.h` resolve via vendor relative includes from applet path. Layer 2 prereq: `DMAMEM` macro defined as no-op in a header included before EnigmaJr (currently in `vec_osc_prereqs.h`; Layer 2 promotes to a more general header like `Arduino.h`). Layer 2 icon stub: not needed (vendor uses generic icons).

Analogue: existing Turing-machine-style applets (none ported yet; this is a new pattern).

#### OffsetQuant

Class B, recipe Class B. Vendor: `applets/OffsetQuant.h:1-182`. Status: in scope.

`OnDataRequest` (`OffsetQuant.h:125`): packed scale + offset (transpose). `Start()` (`OffsetQuant.h:49`): defaults. `Controller()` (`OffsetQuant.h:59`): quantizes In(ch) + offset to scale; outputs.

Test concerns: round-trip; offset=0 yields DualQuant-equivalent behavior; non-zero offset shifts output by N semitones.

Shim prereqs: dep-quant. Layer 2 icon stub: `dualQuantizer` (vendor reuses the icon).

Analogue: DualQuant.

#### MultiScale

Class B, recipe Class B. Vendor: `applets/MultiScale.h:1-186`. Status: in scope.

`OnDataRequest` (`MultiScale.h:109`): packed scale + per-channel weights. `Start()` (`MultiScale.h:36`): default scale Chromatic. `Controller()` (`MultiScale.h:45`): quantizes In(ch) to selectable scale.

Test concerns: round-trip; scale change tracks; Out reflects current scale.

Shim prereqs: dep-quant. Layer 2 icon stub: `multiscale`.

Analogue: DualQuant.

#### ScaleDuet

Class B, recipe Class B. Vendor: `applets/ScaleDuet.h:1-173`. Status: in scope.

`OnDataRequest` (`ScaleDuet.h:95`): packed two scale indices + cross-channel routing. `Start()` (`ScaleDuet.h:34`): two default scales. `Controller()` (`ScaleDuet.h:44`): cross-quantizes channels with mutual influence.

Test concerns: round-trip; default state behavior; scale change on either channel takes effect.

Shim prereqs: dep-quant. Layer 2 icon stub: `scaleDuet`.

Analogue: DualQuant.

#### EnsOscKey

Class B, recipe Class B. Vendor: `applets/EnsOscKey.h:1-390` (large; lots of UI). Status: in scope.

`OnDataRequest` (`EnsOscKey.h:263`): packed key + chord + oscillator settings + ensemble state. `Start()` (`EnsOscKey.h:40`): default key + chord. `Controller()` (`EnsOscKey.h:197`): drives ensemble oscillators; quantizes output to key.

Test concerns: round-trip; key change shifts output by expected semitones; chord change updates output set.

Shim prereqs: dep-quant. Layer 2 icon stub: `note` (vendor uses `NOTE_ICON` at `EnsOscKey.h:328`).

Analogue: Strum.

#### Calibr8

Class B, recipe Class B. Vendor: `applets/Calibr8.h:1-193`. Status: in scope.

`OnDataRequest` (`Calibr8.h:100`): packed calibration offsets + display state. `Start()` (`Calibr8.h:42`): default zero offsets. `Controller()` (`Calibr8.h:47`): applies calibration offsets to In(ch), outputs adjusted CV.

Test concerns: round-trip; identity at zero offset; non-zero offset shifts In by offset.

Shim prereqs: dep-quant (for note display). Layer 2 icon stub: `calibr8`.

Analogue: DualQuant (simpler offset).

### Class C entries

#### ResetClock

Class C, recipe Class C. Vendor: `applets/ResetClock.h:1-192`. Status: in scope (inherits 5-fact verification from Phase 4 abort report).

**5-fact verification (cited from `docs/superpowers/abort-reports/2026-05-18-resetclock-spec-mismatch.md`):**

1. `Clock(1)` is the external Reset input (vendor `ResetClock.h:46-50`). Spec entry inheritance: Tests drive Reset via Clock(1), NOT Clock(0).
2. Reset (ClockOut(1)) fires when `pending_clocks == 1` at output time (vendor `ResetClock.h:62-64`). Tests assert Reset firing as a function of `pending_clocks` evolution over output cycles, NOT Clock(0) edge count.
3. `Start()` (vendor `ResetClock.h:32-40`) does NOT reset `offset_mod`. Test setup must not rely on a fresh `offset_mod` after construction.
4. First-tick `UpdateOffset` (vendor `ResetClock.h:102-107`) jumps `pending_clocks` by the offset value. Tests acknowledge this initial jump in setup.
5. Closest already-ported analogue is **Burst** (pending pulse queue, `RC_TICKS_PER_MS = 175` timing semantics). Tests adopt Burst-style assertions.

`OnDataRequest` (`ResetClock.h:109`): packed length + offset + spacing + per-channel state. `Start()` (`ResetClock.h:32`): default length, default spacing, `offset_mod` NOT reset. `Controller()` (`ResetClock.h:46`): on Clock(1), call vendor `Reset()`; on Clock(0), increment `pending_clocks`; per spacing*175 ticks, fire one queue entry on output cycle.

Test concerns: round-trip (10x multiplier inherited: 1 Clock(0) edge becomes 10 `pending_clocks++` due to 10 Controller calls per buffer; use round-trip + state-injection, NOT bus-level fire-count); Reset semantics via Clock(1) at known `pending_clocks` state; `OC::CORE::ticks` advancement assertion via `step_n_inner_ticks(loaded, alg, bus, spacing * 175)` matching one output cycle. Use Burst-style assertion shape per CLAUDE.md.

Shim prereqs: Phase 5 time-injection helper `step_n_inner_ticks` (shipped). Layer 2 icon stub: `resetClk`.

Analogue: Burst (`harness/tests/test_hemispheres.cpp` Burst entries).

#### Shuffle

Class C, recipe Class C. Vendor: `applets/Shuffle.h:1-191`. Status: in scope.

`OnDataRequest` (`Shuffle.h:113`): packed shuffle pattern + tick offsets. `Start()` (`Shuffle.h:31`): default 50% shuffle (no shuffle). `Controller()` (`Shuffle.h:49`): on Clock(0), advances internal step position with per-step shuffle offset; outputs delayed gates.

Test concerns: round-trip; shuffle=50 yields straight pattern (no delay); shuffle=75 delays even ticks; assertion uses `step_n_inner_ticks(loaded, alg, bus, N)` for tick-budget verification; pair with short-N control test (N=10).

Shim prereqs: time-injection helper. Layer 2 icon stub: `shuffle`.

Analogue: existing gate-delay-style applets (GateDelay).

#### Xfader

Class C, recipe Class C. Vendor: `applets/Xfader.h:1-158`. Status: in scope.

`OnDataRequest` (`Xfader.h:94`): packed crossfade position + curve. `Start()` (`Xfader.h:32`): default 50/50 mix. `Controller()` (`Xfader.h:40`): blends In(0) and In(1) by crossfade position; CV input modulates position; outputs mix and inverse mix.

Test concerns: round-trip; position=0 yields In(0) on Out(0); position=128 yields balanced mix; CV input modulates correctly; use `step_n_inner_ticks` for time-windowed assertions.

Shim prereqs: time-injection helper. Layer 2 icon stub: `mixerBal`.

Analogue: existing CV-mix style applets.

#### Scope

Class C, recipe Class C. Vendor: `applets/Scope.h:1-254`. Status: in scope.

`OnDataRequest` (`Scope.h:133`): packed buffer write position + display range. `Start()` (`Scope.h:40`): zeros buffer. `Controller()` (`Scope.h:50`): per-tick samples In(ch) into ring buffer; outputs reproduce buffered samples (or do not, depending on mode).

Test concerns: round-trip; buffer fills over `step_n_inner_ticks(loaded, alg, bus, N)` calls with known input pattern; buffer wraps correctly at N greater than buffer size; output mode passes through correctly.

Shim prereqs: time-injection helper. Layer 2 icon stub: `scope`.

Analogue: existing buffer-style applets.

### Class D entries

#### Metronome

Class D, recipe Class D. Vendor: `applets/Metronome.h:1-136`. Status: in scope.

`OnDataRequest` (`Metronome.h:68`): returns 0 (no state to persist). NO pack helper; test asserts `OnDataRequest() == 0` directly. `Start()` (`Metronome.h:29`): registers with `clock_m` (Phase 5 dep-clock-mgr ClockManager). `Controller()` (`Metronome.h:35`): drives `clock_m.SetTempoBPM`; emits Tock(ch) per beat via ClockOut(ch).

Test concerns: assertion that `OnDataRequest() == 0`; ClockManager state probe (`clock_m.SetTempoBPM(120)`, advance N inner ticks, verify Tock fires at expected rate); pair with `step_n_inner_ticks` for budgeted tick advancement.

Shim prereqs: dep-clock-mgr (Phase 5 shipped `clock_m` global + step() prologue hook). Layer 2 icon stub: `metronome_L` (vendor uses `METRO_L_ICON` at `Metronome.h:27`).

Analogue: dep-clock-mgr test patterns from `harness/tests/test_dep_clock_mgr.cpp`.

## Spec footer

### Recipe spot-check (3 claims across the 4 recipe classes)

1. **Class A: dep-vec-osc tolerance class is float, 1 LSB on int16 output.** Verified against Phase 5 `harness/tests/test_dep_vec_osc.cpp:1-12` header comment which states "float-using, 1-LSB tolerance on int16 output". Phase 6 Class A entries (VectorLFO, VectorEG, VectorMod, VectorMorph, Relabi) inherit this tolerance.

2. **Class B: scales referenced by name, not numeric index.** Verified against kickoff recipe section "Tests reference scales by name (e.g., 'Chromatic', 'Major') not by numeric index, since indices may shift if the table is reordered." Phase 6 Class B entries (Pigeons through Calibr8) follow this convention.

3. **Class C: ResetClock uses Burst-style assertion shape, not ClkToGate.** Verified against Phase 4 abort report finding (5). Spec entry for ResetClock above cites all 5 facts including the analogue.

### Per-entry verification (3 randomly-selected entries traced against vendor source)

1. **DualQuant** (Class B): Spec cites `Start()` at `DualQuant.h:31`, `Controller()` at `DualQuant.h:40`, `OnDataRequest()` at `DualQuant.h:80`. Verified by direct vendor read: matches.

2. **LowerRenz** (Class A): Spec cites `Start()` at `LowerRenz.h:36`, `Controller()` at `LowerRenz.h:41`, `OnDataRequest()` at `LowerRenz.h:82`. Verified by direct vendor read: matches.

3. **Metronome** (Class D): Spec cites `Start()` at `Metronome.h:29`, `Controller()` at `Metronome.h:35`, `OnDataRequest()` returns 0 at `Metronome.h:68`. Verified by direct vendor read: matches.

All three verifications pass. If 2+ of 3 had failed, every entry would be audited; here, audit is bounded to spot-check.

### Shim prereq verification

All Phase 5 dep surface artifacts referenced in Class A entries exist in shim at `dr/phase6-applets-plan` head:

- `shim/include/vector_osc/HSVectorOscillator.h`
- `shim/include/vector_osc/WaveformManager.h`
- `shim/include/vector_osc/vec_osc_prereqs.h`
- `shim/include/HSRelabiManager.h`
- `shim/include/lorenz/streams_lorenz_generator.h`
- `shim/include/lorenz/streams_resources.h`
- `shim/include/HSLorenzGeneratorManager.h`
- `shim/include/CVInputMap.h` (Phase 5 dep-cv-map)
- `shim/include/HSClockManager.h` (Phase 5 dep-clock-mgr full)
- `shim/include/quant/braids_quantizer.h`, `braids_quantizer_scales.h`, `OC_scales.h`, `MIDIQuantizer.h`

`HS::Quantize`, `HS::SetScale`, `HS::GetScale`, `HS::GetRootNote`, `HS::SetRootNote` (used by Class B recipe) are surfaced from `shim/include/HSUtils.h` per Phase 5 dep-quant glue. Verified at `8f42965`.

`step_n_inner_ticks(loaded, alg, bus, N)` (used by Class C and Class D recipes) is in `harness/tests/applet_test_helpers.h` per Phase 5 helper. Verified at baseline.

`clock_m.SetTempoBPM` and the step() prologue hook (used by Class D recipe) are in `shim/include/HSClockManager.h` per Phase 5 dep-clock-mgr. Verified at baseline.
