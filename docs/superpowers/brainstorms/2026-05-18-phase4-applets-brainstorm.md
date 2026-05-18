# Brainstorm: Phase 4 Hemisphere applet ports

Date: 2026-05-18
Status: Draft
Owner: Dan
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`

## Context

Phase 3 retry landed 10 category-A Hemisphere applet ports on `main` at squash commit `87596f4`. Five applets that Phase 3 attempt 1 over-scoped were deferred to Phase 4: Binary, ResetClock, ShiftGate, Trending, ProbabilityDivider. The retrospective at `docs/superpowers/abort-reports/2026-05-18-phase3-attempt-1-retrospective.md` enumerates the root causes. The ResetClock-specific defect report at `docs/superpowers/abort-reports/2026-05-18-resetclock-spec-mismatch.md` enumerates the vendor-semantics findings.

Phase 4 scope: ship the deferrals plus a bounded set of category-B (modulate-heavy) applets. The kickoff caps category-B at 5-8 applets. Total Phase 4 target: 10-13 applets. After audit, ResetClock demotes to Phase 5 and category-B clean candidates total 3. Phase 4 ships 7 applets.

## Single-applet scope (re-affirmed)

Pair-applet variants remain deferred. Phase 4 ports single-applet plug-ins only.

## Phase 3 retry shim additions inventory

Phase 3 retry's integration commit (`a0a815b`, squashed into `87596f4`) added the following to the shim. Each is verified against HEAD on `main`:

- Macros (in `shim/include/HSUtils.h`): `HEMISPHERE_3V_CV` at line 13, `HEMISPHERE_PULSE_ANIMATION_TIME` at line 14, `HEMISPHERE_PULSE_ANIMATION_TIME_LONG` at line 15, `ForAllChannels` at line 28.
- Icons (in `shim/include/HSicons.h`): `CHECK_ON_ICON`, `CHECK_OFF_ICON`, `STAIRS_ICON`, `UP_BTN_ICON`, `RESET_ICON` at lines 18-22.
- Headers (in `shim/include/HemisphereApplet.h`): `<cmath>` at line 3 (for `logf`).
- RNG: `hem_shim_random` xorshift32 + `random()` macro at HemisphereApplet.h:212-221.

These are NOT Layer 0 work for Phase 4. The Phase 3 retry inventory is the baseline from which Phase 4 builds.

## Operational category-B boundary

The Phase 3 brainstorm stub said "modulate-heavy; folded into A; Phase 2 covers CV-input under existing harness". That is not load-bearing. Phase 4 needs an auditable predicate.

**Boundary predicate (an applet is category-B if all five hold):**

1. **Vendor predicate**: Controller body uses `In(ch)`, `DetentedIn(ch)`, `Modulate(...)`, or `Gate(ch)` for CV-driven parameter modulation. May also use `Clock(ch)` provided clock state isn't the dominant logic.
2. **Internal state**: has accumulator, integrator, or bounded state machine that evolves per Controller tick. State must be bounded (envelope amplitude, board population, position in N-step ring) so the 10x clocked multiplier produces a determinable post-buffer state.
3. **Forbidden time logic**: NO reads of `millis()`, `micros()`, `OC::CORE::ticks` in Controller. NO state machines where transitions depend on wall-clock duration. (Tick-count thresholds against internal counters are OK.)
4. **Forbidden external deps**: NO calls to `Quantize(...)`, `HS::QuantizerLookup`, `MIDIQuantizer`, `SemitoneIn(...)` (which depends on quantizer), `CVInputMap` member access, `MIDISend(...)`, audio DSP classes (`peaks::*`, `wave`, `tone`), or scales (`OC::Scales::*`).
5. **Harness change required**: none beyond what's already shipped, OR what Layer 0 of this plan adds. Specifically: no new step-stepping helper, no time-injection helper, no ADC-lag injection helper.

**Borderline disposition rule**: if predicate (2) is met by bounded state that resembles a sequencer ring buffer (step array, pattern array, accent[] / note[]), the applet is category-D not category-B; defer. If predicate (2) is met by an integrator that converges (envelope) or a closed system (cellular automaton with finite cells), the applet is category-B.

### Fit example

**ADEG** (`vendor/O_C-Phazerville/software/src/applets/ADEG.h`): Controller reads `DetentedIn(0)` and `DetentedIn(1)` to modulate per-segment durations. State machine: `phase in {0,1,2}` plus `signal` integrator bounded by `HEMISPHERE_MAX_CV`. Clock(0) triggers, Clock(1) reverse-triggers. No time-based logic. No external deps. Test pattern: model the 10x integrator math (Stairs ST2-ST3 / Cumulus CU2 precedent).

### Borderline example

**GameOfLife** (`vendor/O_C-Phazerville/software/src/applets/GameOfLife.h`): Controller reads `In(0)` and `In(1)` for `tx`/`ty` positions. State: 80-byte board, fixed-seed pattern at Start() (no RNG dep). Predicate (2) borderline because the board state evolves on every Clock(0), and the state is technically D-shaped (board cells). But the predicate flips to B because the cells are closed and the CV inputs drive position, not sequence steps. Admit with explicit note in spec.

### Excluded borderline

**Combin8** (`vendor/O_C-Phazerville/software/src/applets/Combin8.h`): Controller is pure CV-add (matches predicate 1) but depends on `CVInputMap sources[2][2]` member access for the extra CV inputs. `CVInputMap::ChangeSource`, `EditSelectedInputMap`, `PackPackables`, `UnpackPackables` are not in the shim and are not Phase 4 work. Excluded by predicate 5. Defer to Phase 5 with CVInputMap shim porting.

## Category-B candidate enumeration

Under the boundary above, the audited vendor inventory (86 applet headers, 24 already ported) yields the following candidates. Already-ported applets are excluded; category-C/D/E/F/G/H/I exclusions follow the Phase 3 brainstorm's classification with the time-logic and external-deps predicates applied uniformly.

| Applet | Pred 1 (CV mod) | Pred 2 (bounded state) | Pred 3 (no time) | Pred 4 (no ext deps) | Pred 5 (no new harness) | Admit |
| --- | --- | --- | --- | --- | --- | --- |
| ADEG | yes (DetentedIn) | yes (phase+signal) | yes | yes | yes | yes |
| ADSREG | yes (cv_mod) | yes (stage+amplitude) | yes | yes (self-contained MiniADSR) | yes | yes |
| GameOfLife | yes (In(0)/In(1) for tx/ty) | yes (board, fixed-seed Start) | yes | yes | yes | yes (borderline) |
| Combin8 | yes | yes | yes | NO (CVInputMap dep) | NO | no (defer P5) |
| Calibr8 | yes | yes | yes | NO (MIDIQuantizer) | NO | no (E) |
| Carpeggio | yes | yes | yes | NO (Quantize) | NO | no (E) |
| Chordinator | yes | yes | yes | NO (Quantize) | NO | no (E) |
| DualQuant, OffsetQuant, Squanch, Pigeons, EnsOscKey, MultiScale | yes | varies | yes | NO (quantizer) | NO | no (E) |
| ScaleDuet, DuoTET | yes | yes | yes | NO (scales) | NO | no (F) |
| BootsNCat, BugCrack, DrLoFi, DrumMap, Squanch, Tuner, WTVCO, Xfader, TwoRings, BitBeat | varies | n/a | varies | NO (audio DSP) | NO | no (H) |
| EbbAndLfo, LowerRenz, VectorEG, VectorLFO, VectorMod, VectorMorph, Relabi | yes | yes | NO (time-based phase) | varies | NO | no (C) |
| Strum, Shuffle, Xfader, ResetClock | varies | varies | NO (millis/ticks/RC_TICKS) | yes | NO | no (C) |
| DivSeq, DivSeq10, EnvSeq, EuclidX, EnigmaJr, MiniSeq, Palimpsest, Pigeons, ProbabilityMelody, Seq32, SeqPlay7, SequenceX, Shredder, SwitchSeq, TB3PO, TrigSeq, TrigSeq16, CVRecV2 | varies | NO (step/pattern arrays) | varies | varies | NO | no (D) |
| hMIDIIn, hMIDIOut, MidiLoop | n/a | n/a | n/a | NO (MIDI) | NO | no (G) |
| ClockSetup, ClockSetupT4, Metronome, Scope | n/a | n/a | n/a | NO (system) | NO | no (I) |

Admitted candidates: 3.

### Selection criteria (priority order)

The kickoff caps category-B at 5-8 and asks for ranked criteria. With 3 admitted candidates, the cap binds nothing on the upper side and the cat-B count falls below the kickoff lower bound. The ranked criteria are recorded anyway for audit:

1. No RNG in Controller (otherwise seed-hem-rng dependency).
2. OnDataRequest packed in <=40 bits (= Burst's existing max).
3. No time-based logic (already in boundary).
4. Controller body <=40 lines.
5. Has a close already-ported analogue.

Scoring of the 3 admitted candidates:

| Applet | RNG | Pack bits | Time | Controller LOC | Analogue | Sum |
| --- | --- | --- | --- | --- | --- | --- |
| ADEG | yes (no RNG) | 16 | yes (no time) | ~50 (with branches) | Slew (Slew is single CV-out integrator) | 5 |
| ADSREG | yes | 64 | yes | ~10 (defers to MiniADSR) | EnvFollow (peak tracker) | 5 |
| GameOfLife | yes | 6 | yes | ~15 (board work in helpers) | RunglBook (state machine on Clock(0)) | 5 |

All three pass all five criteria. All three admit. No filtering needed.

## Cat-B scarcity finding (load-bearing decision)

Cat-B clean candidates total 3 under the boundary. The Phase 3 brainstorm cat-A sweep + the Phase 3 retry's already-shipped 10 ports captured most CV-modulation-only applets from the vendor inventory. The remaining vendor headers are dominated by categories C/D/E/G/H/I.

The kickoff abort budget triggers if "the category-B boundary admits fewer than 3 candidates, reducing Phase 4 total scope below 8 applets". The boundary admits exactly 3 candidates, meeting the >=3 floor. Total Phase 4 scope = 7 applets (4 deferrals after ResetClock demotion + 3 cat-B), which is below 8 but the abort condition phrases the cause as "fewer than 3 candidates", not the consequence count. Phase 4 proceeds with 7 applets.

**Load-bearing decision**: Phase 4 ships 7 applets instead of the 10-13 target because the vendor inventory genuinely lacks more clean cat-B candidates under the pinned SHA. Phase 5 will broaden scope by porting CVInputMap (unblocks Combin8), MIDIQuantizer / scales (unblocks E/F bucket), peaks envelopes (unblocks D-ish bucket), and a category-C time-injection helper (unblocks ResetClock and time-LFO families).

## Phase 4 in-scope applet list (target: 7)

### Deferrals from Phase 3 (4 of 5)

1. **Binary** (`Binary.h`). Layer 0 prereq: shim `SegmentDisplay` stub class + `PhzIcons::binaryCounter` icon. `HEMISPHERE_3V_CV` already in shim. OnDataRequest returns 0 (no pack helper). Status: in scope. Cherry-pick: re-implement.
2. **ShiftGate** (`ShiftGate.h`). Layer 0 prereq: `LOOP_ICON` and `X_NOTE_ICON` in `HSicons.h` (also unblocks ProbabilityDivider for `LOOP_ICON`) + `PhzIcons::shiftGate` icon. `HEMISPHERE_3V_CV` already in shim, `random()` already in shim. Status: in scope. Cherry-pick: re-implement (the Phase 3 attempt-1 dirty archive is stale relative to current shim state).
3. **Trending** (`Trending.h`). Layer 0 prereq: `HemisphereApplet::Changed(int ch)` method (reads `HS::frame.changed_cv[ch + channel_offset()]`) + update `changed_cv` from frame-to-frame input deltas in shim step() + `PhzIcons::trending` icon. Status: in scope. Cherry-pick: re-implement.
4. **ProbabilityDivider** (`ProbabilityDivider.h`). Layer 0 prereq: `LOOP_ICON` (shared with ShiftGate) + `DrawSlider` method on HemisphereApplet (View only) + `randomSeed(uint32_t)` reseeds `hem_rng_state` + `micros()` shim returning a monotonic uint32_t + stub `ProbLoopLinker` singleton (in shim; `RegisterDiv`, `UnloadDiv`, `SetLooping`, `TriggerRegeneration`, `SetLoopStep`, `Trigger`, `GetSeed`, `SetSeed`, `IsLinked` no-op stubs that maintain `seed` state) + `PhzIcons::probDiv` icon. `HEMISPHERE_PULSE_ANIMATION_TIME[_LONG]` already in shim. Status: in scope. Cherry-pick disposition: re-implement; the archived `phase3-retry/prob_div` test commit predates the Layer 0 ProbLoopLinker stub and may have made different assumptions about reseed semantics, so re-implementing is cleaner than rebasing.

### Deferred (1 of 5: demoted to Phase 5)

- **ResetClock** (`ResetClock.h`). Demoted to Phase 5. Reason: time-injection required. spacing=6 * RC_TICKS_PER_MS=175 / ticks_this_step=10 = 105 step() calls per output cycle. Even minimum `spacing=1` requires 17.5 step() calls per output cycle. Phase 4 explicitly excludes category-C time-injection helper. Carry-forward: Phase 5 designs the time-injection helper that lets a test advance the shim's `OC::CORE::ticks` budget without driving 100+ step() calls per assertion, then re-anchors the ResetClock spec on Burst (per the abort report: pending-pulse queue with `RC_TICKS_PER_MS=175` timing) and rewrites RC2/RC3 test cases against the corrected `pending_clocks` semantics.

### Category-B (3 of 5-8 cap)

- **ADEG** (`ADEG.h`). Layer 0 prereq: `PhzIcons::adsrEg` (or appropriate icon for ADEG; vendor uses `PhzIcons::adsrEg` for ADSREG; for ADEG check applet_icon at line of vendor source). Status: in scope. Closest analogue: Slew (single CV-out, two signed time-domain params).
- **ADSREG** (`ADSREG.h`). Layer 0 prereq: `PhzIcons::ADSR_EG` icon bitmap (defined at vendor PhzIcons.h:60). Self-contained `MiniADSR` inner struct (no shim port). Status: in scope. Closest analogue: EnvFollow (peak/envelope tracking with gain+duck+speed pack).
- **GameOfLife** (`GameOfLife.h`). Layer 0 prereq: `PhzIcons::gameOfLife` icon. Status: in scope (borderline cat-B with explicit board-state note in spec). Closest analogue: RunglBook (state machine driven by Clock with CV-thresholded inputs).

## Cherry-pick disposition for `phase3-retry/prob_div`

The local `phase3-retry/prob_div` branch contains a Phase 3 retry test commit for ProbabilityDivider that was not merged because the Layer 0 shim work (ProbLoopLinker stub, `randomSeed`/`micros` shims, `DrawSlider`, `LOOP_ICON`) is a Phase 4 deliverable.

**Disposition**: re-implement, do NOT cherry-pick. Justification:

1. The Phase 4 Layer 0 shim adds `ProbLoopLinker` as a stub singleton. The archived test commit may have inlined assumptions about seed behavior that diverge from the stub's actual semantics. Re-implementing against the stub is auditable; rebasing the archived commit is not.
2. The Phase 4 spec entry for ProbabilityDivider (see spec) cites the vendor `OnDataRequest` layout against the current pinned SHA; if the archived commit's pack-helper signature diverges from the spec, the spec wins. Re-implementing is straightforward.
3. The archived commit landed under Phase 3 retry's spec, which framed ProbabilityDivider as "round-trip + state-injection only" (no bus-level fire-count assertions). The Phase 4 spec retains this framing, so the test bodies will be structurally similar but the helper signatures and the `LOOP_ICON` reference point are fresh.

## Phase 5 carry-forward

Items deferred from Phase 4 that Phase 5 will tackle:

- **ResetClock**: needs category-C time-injection helper (advance `OC::CORE::ticks` budget without driving 100+ step() calls per assertion). Spec rewrite against the abort report's 5 facts.
- **CVInputMap port**: unblocks Combin8 and any other applet with per-channel extra CV sources.
- **MIDIQuantizer / scales port**: unblocks category E/F applets (DualQuant, OffsetQuant, Calibr8, Carpeggio, Chordinator, Pigeons, EnsOscKey, MultiScale, ScaleDuet, DuoTET, Squanch).
- **peaks envelopes / audio DSP**: unblocks category H applets (BootsNCat, BugCrack, DrLoFi, DrumMap, WTVCO, etc.), but most of those are intentionally out of scope as they don't fit the CV-control plug-in shape.
- **Category-D stateful sequencers**: needs a sequencer harness pattern. Many vendor applets (DivSeq, EnvSeq, EuclidX, MiniSeq, Palimpsest, Seq32, SeqPlay7, SequenceX, Shredder, SwitchSeq, TB3PO, TrigSeq, TrigSeq16, CVRecV2, etc.).
- **Time-based LFO/EG**: VectorLFO, VectorMod, VectorMorph, VectorEG, EbbAndLfo, Relabi, LowerRenz once the time-injection helper exists.

## Layer 0 shim inventory (sequenced commits in plan)

Layer 0 work in plan layer order. Each commit lands one at a time on `dr/phase4-applets-plan` with `make test-applets` and `make arm` passing before the next is started.

1. **LOOP_ICON + X_NOTE_ICON** in `shim/include/HSicons.h` + bitmap content in `shim/src/icons.cpp`. Unblocks: ShiftGate, ProbabilityDivider.
2. **HemisphereApplet::Changed(int ch)** + `changed_cv` update in shim `step()`. Unblocks: Trending.
3. **DrawSlider method** on HemisphereApplet (View only; no Out() impact). Unblocks: ProbabilityDivider.
4. **randomSeed(uint32_t) + micros()** in shim. randomSeed reseeds `hem_rng_state`. micros returns `OC::CORE::ticks` cast to uint32_t (sufficient for vendor's use as RNG seed). Unblocks: ProbabilityDivider.
5. **ProbLoopLinker stub singleton** in `shim/include/HSProbLoopLinker.h` (mirrors vendor file path) + include path made visible to applets. Unblocks: ProbabilityDivider.
6. **SegmentDisplay stub class** in `shim/include/SegmentDisplay.h` (mirrors vendor file path) with no-op render methods. Unblocks: Binary.
7. **Icon bitmaps for in-scope applets** in `shim/src/icons.cpp` + extern decls in `shim/include/PhzIcons.h`: `binaryCounter`, `shiftGate`, `trending`, `probDiv`, `adsrEg` (vendor `PhzIcons::ADSR_EG` is the identifier for ADSREG), and an icon for ADEG and GameOfLife (vendor uses `PhzIcons::adsrEg` for ADEG also; verify in spec). Unblocks: all in-scope applets except those without an icon. (Note: ADEG may share `PhzIcons::adsrEg` with ADSREG; spec verifies.)

ResetClock-only shim prereqs (HEMISPHERE_CLOCK_TICKS plus the time-injection helper) are NOT included; ResetClock is deferred to Phase 5.

Total Layer 0 commit count: 7. Layer 0 wall-clock cost target: ~1 hour (mostly bitmap copy + 1-line method additions; ProbLoopLinker stub is the largest at ~30 lines).

## Goal counts

- Phase 3 retry shim additions reverified: present. No re-add work.
- Phase 4 Layer 0 shim additions: 7 sequenced commits.
- Phase 4 in-scope applets: 7 (4 deferrals + 3 cat-B).
- Phase 4 deferred to Phase 5: 1 (ResetClock).
- Phase 4 total wall-clock target: roughly the slowest single implementer task plus integration plus verification (cf. parallel-execution rule).

## Open questions handled

- Cat-B operational boundary: defined above with 5 predicates and a borderline disposition rule.
- ResetClock category: confirmed C, demoted to Phase 5.
- ProbabilityDivider cherry-pick: re-implement, not cherry-pick.
- Phase 4 total scope below kickoff target: load-bearing decision recorded; carry-forward documented.

Phase 4 proceeds to spec.
