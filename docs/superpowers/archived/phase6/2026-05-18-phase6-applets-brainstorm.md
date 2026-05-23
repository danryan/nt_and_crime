# Brainstorm: Phase 6 Hemisphere applet port batch

Date: 2026-05-18
Status: Active
Owner: Dan
Branch: `dr/phase6-applets-plan`
Worktree: `.worktrees/phase6-applets-plan`
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`; `vendor/distingNT_API` at `cd12d876`.
Kickoff: `docs/superpowers/prompts/2026-05-18-phase6.md`.

## Scope

Port 25 Hemisphere applets against the Phase 5 dep surface in a single Layer 1 parallel fan-out. Phase 5 (squash `8f42965`) shipped six vendor deps plus the time-injection harness helper plus the `HS::Quantize` pool on `HemisphereApplet`. Phase 6 consumes that surface and adds applet tests + factory entries; it does NOT modify Phase 5 deps or the compiler-rt vendoring.

The phase ends with `make test-applets` adding 25 applet test groups (final case count baseline 157 + per-applet additions), `make test-deps` unchanged at 60 cases / 8564 assertions, `make arm` at 0 errors and 3 accepted warnings, and `Hemispheres.o` loading on hardware with the Phase 5 trio still green plus at least one Phase 6 newly-landed pair green.

## Reconciliation of "28 named candidates" vs "25 confirmed in-scope"

The kickoff prompt lists 28 named candidates across four classes (A: 9, B: 14, C: 4, D: 1). The audit demotes three for Phase 5 dep surface gaps and confirms the remaining 25.

Demotions (deferred to Phase 7+):

- **WTVCO** (Class A, vec-osc-adjacent): vendor `applets/WTVCO.h:21` includes `<arm_math.h>` (CMSIS-DSP). Phase 5 did NOT vendor CMSIS-DSP. Phase 5 dep surface gap. WTVCO needs a CMSIS-DSP dep port before it can land.
- **DuoTET** (Class B, quantizer): vendor `applets/DuoTET.h:36` includes `<arm_math.h>`. Same gap.
- **EbbAndLfo** (Class A, tideslite): vendor `applets/EbbAndLfo.h` requires three Phase 5 dep surface extensions to compile under c++11 ARM: (a) `ComputePhaseIncrement` in vendor `tideslite.h:154` is a multi-statement constexpr (c++14-only) and the shim copy is byte-identical; (b) vendor `util_phase_extractor.h:25-26` calls `max` without a using-declaration; (c) vendor base `HemisphereApplet.h:169` declares `SemitoneIn(int ch)` as a member that the shim base omits; (d) vendor base `HemisphereApplet.h:102` declares `virtual void DrawFullScreen() { View(); }` that the shim base omits. Plus an `ebb_n_Flow` PhzIcons stub. Five shim-side touchpoints for a single applet is enough integration friction that EbbAndLfo is its own Phase 7+ entry, not a Phase 6 implementer task.

Final in-scope inventory: 28 - 3 = 25. Resolves cleanly to the kickoff's 25-confirmed target.

The kickoff's "Explicitly deferred to Phase 7+" list (Drum1, Drum2, ASR-Drums) is independent of the 28-named candidates. Those three were never in the Phase 6 candidate pool; they are vec-osc-family applets that need re-audit against the shipped Phase 5 surface and are inventoried separately for Phase 7+.

## Final in-scope inventory (25 applets, four classes)

Class A — vendor-dep (uses Phase 5 vec-osc, lorenz, cv-map surfaces): 7 applets.

| Applet | Vendor file (lines) | Required Phase 5 dep | Recipe |
|---|---|---|---|
| VectorLFO | `applets/VectorLFO.h` (246) | dep-vec-osc | Class A |
| VectorEG | `applets/VectorEG.h` (234) | dep-vec-osc | Class A |
| VectorMod | `applets/VectorMod.h` (176) | dep-vec-osc | Class A |
| VectorMorph | `applets/VectorMorph.h` (179) | dep-vec-osc | Class A |
| Relabi | `applets/Relabi.h` (579) | dep-vec-osc (RelabiManager) | Class A |
| LowerRenz | `applets/LowerRenz.h` (134) | dep-lorenz | Class A |
| Combin8 | `applets/Combin8.h` (155) | dep-cv-map | Class A |

Class B — quantizer (uses Phase 5 `HS::Quantize` pool): 13 applets.

| Applet | Vendor file (lines) | Recipe |
|---|---|---|
| Pigeons | `applets/Pigeons.h` (201) | Class B |
| Strum | `applets/Strum.h` (276) | Class B |
| Shredder | `applets/Shredder.h` (349) | Class B |
| Carpeggio | `applets/Carpeggio.h` (268) | Class B |
| Squanch | `applets/Squanch.h` (210) | Class B |
| Chordinator | `applets/Chordinator.h` (187) | Class B |
| DualQuant | `applets/DualQuant.h` (142) | Class B |
| EnigmaJr | `applets/EnigmaJr.h` (207) | Class B |
| OffsetQuant | `applets/OffsetQuant.h` (182) | Class B |
| MultiScale | `applets/MultiScale.h` (186) | Class B |
| ScaleDuet | `applets/ScaleDuet.h` (173) | Class B |
| EnsOscKey | `applets/EnsOscKey.h` (390) | Class B |
| Calibr8 | `applets/Calibr8.h` (193) | Class B |

Class C — helper-using (uses Phase 5 `step_n_inner_ticks`): 4 applets.

| Applet | Vendor file (lines) | Recipe |
|---|---|---|
| ResetClock | `applets/ResetClock.h` (192) | Class C (Burst analogue) |
| Shuffle | `applets/Shuffle.h` (191) | Class C |
| Xfader | `applets/Xfader.h` (158) | Class C |
| Scope | `applets/Scope.h` (254) | Class C |

Class D — clock-mgr immediate (uses Phase 5 `clock_m` BPM/Tock/Cycle): 1 applet.

| Applet | Vendor file (lines) | Recipe |
|---|---|---|
| Metronome | `applets/Metronome.h` (136) | Class D |

Total: 7 + 13 + 4 + 1 = 25.

## Demotion thresholds (each below halt)

- Class A: 2 of 9 demote (WTVCO, EbbAndLfo). Halt threshold: more than 3.
- Class B: 1 of 14 demote (DuoTET). Halt threshold: more than 4.
- Class C: 0 of 4 demote. Halt threshold: more than 2.
- Class D: 0 of 1 demote.

No class trips its halt threshold.

## Class boundaries (each unambiguous)

Class assignment is deterministic and follows the kickoff's class definitions. Borderline-check audit:

- **Carpeggio** uses both the quantizer pool AND `hem_arp_chord.h` (vendor arpeggiator helper). Quantizer is the dominant interaction. Class B.
- **Calibr8** is a calibration applet that uses the quantizer for note display. Class B by quantizer use.
- **EnsOscKey** uses the quantizer for note quantization but also touches `NOTE_ICON` (icon stub at Layer 2). Class B.
- **Scope** is helper-using (multi-buffer waveform capture). Class C.
- **Metronome** is the only clock-mgr immediate applet. Class D.

No applet spans two classes ambiguously.

## ResetClock 5-fact verification (re-confirmed against vendor at SHA 7800d929)

Cited in the Phase 4 abort report `docs/superpowers/abort-reports/2026-05-18-resetclock-spec-mismatch.md`. Re-traced for Phase 6:

1. `Clock(1)` is the external Reset input (vendor `ResetClock.h:46-50`). Spec entry must call this out.
2. Reset (ClockOut(1)) fires when `pending_clocks == 1` at output time (vendor `ResetClock.h:62-64`), NOT when `position == offset`.
3. `Start()` (vendor `ResetClock.h:32-40`) does NOT reset `offset_mod`. Tests must not assume a fresh `offset_mod` after construction.
4. First-tick `UpdateOffset` (vendor `ResetClock.h:102-107`) jumps `pending_clocks` by the offset value. With non-zero `offset_mod` at construction, Reset fires earlier than naive Clock(0)-edge-counting would suggest.
5. Closest already-ported analogue is **Burst** (pending pulse queue, `RC_TICKS_PER_MS = 175` timing). NOT ClkToGate.

All five facts trace to current vendor source at the pinned SHA.

## Phase 7+ carry-forward inventory

Phase 6 defers these applets and surfaces to the Phase 7+ planning conversation:

- **arm_math.h dep**: WTVCO, DuoTET. Requires a CMSIS-DSP dep port (new dep surface, separate Phase 7 entry).
- **tideslite full integration**: EbbAndLfo. Requires Phase 5 dep extensions (constexpr c++14 build flag or vendor-equivalent rewrite, `SemitoneIn`, `DrawFullScreen` base class additions, `max` declaration fix, `ebb_n_Flow` icon).
- **vec-osc family re-audit (kickoff-deferred, not in 28-named pool)**: Drum1, Drum2, ASR-Drums.
- **Long-term clock-aware set (kickoff-deferred)**: ClockSetup, ClockSetupT4, and the open-ended clock-aware applet inventory. Phase 7+ enumerates.
- **Other vendor applets at `7800d929`** not in any Phase 6 candidate pool (87 vendor headers - 31 Phase 5 baseline - 25 Phase 6 in-scope - 3 Phase 6 demoted - 3 kickoff-deferred = 25 remaining for future audit).

## Integration-time touchpoints (Layer 2 owns)

Phase 6 needs the following at integration, none of which are dep surface modifications:

- **PhzIcons additions** (~16 icons): `singing_pigeon`, `strum`, `shredder`, `carpeggio`, `squanch`, `chordinate`, `dualQuantizer`, `multiscale`, `scaleDuet`, `calibr8`, `resetClk`, `shuffle`, `mixerBal`, `scope`, `metronome_L`, `note`. Layer 2 adds entries to `shim/include/PhzIcons.h` and bitmaps to `shim/src/icons.cpp`. Per CLAUDE.md "To add an applet, the integration step touches both plus shim/include/PhzIcons.h and shim/src/icons.cpp for icon stubs."
- **DMAMEM visibility**: vendor `enigma/TuringMachine.h:65` uses Teensy `DMAMEM`. Phase 5 defined it as no-op in `shim/include/vector_osc/vec_osc_prereqs.h`. Layer 2 moves the no-op `#define DMAMEM` to a header included before EnigmaJr (likely `Arduino.h` or a new `shim/include/teensy_compat.h`). Bounded.
- **Pre-include Phase 5 dep prereqs in HemispheresFactory.h**: Vendor relative includes from `applets/*.h` pull vendor's `util/util_math.h` (Proportion redefinition) and `util/util_macros.h` if not preempted. Layer 2 ensures `HemispheresFactory.h` includes `vector_osc/vec_osc_prereqs.h` (and equivalent for lorenz/quant/cv-map if needed) BEFORE pulling vec-osc applet headers. Include guards on the shim copies absorb subsequent vendor re-includes.
- **`gfxDisplayInputMapEditor` on shim base** (Combin8 only): vendor `HemisphereApplet.h:595` defines this as a member; shim base omits it. Layer 2 ports the member onto shim `HemisphereApplet.h`, delegating to `cvmap` global. Bounded (~10 LOC).
- **`applet_indices.h`, `HemispheresFactory.h`, factory table alpha-ordered inserts**: standard integration. 25 new entries in each.

None of these touch Phase 5 dep files (`shim/include/vector_osc/*`, `shim/include/lorenz/*`, `shim/include/tideslite/*`, `shim/include/quant/*`, `shim/include/cv_map/*` if it existed, `shim/include/CVInputMap.h`). The pre-commit hook hard-rejects any `phase6-port/*` commit touching these.

## Helper-design carry-forward (no new helpers)

Phase 5 shipped `step_n_inner_ticks(loaded, alg, bus, N)`. Phase 6 Class C applets call it as-is. Phase 5 helper invariant tests (Cumulus equivalence at N=10; tick-advancement at N=1000 against Empty + held gate) prove the helper. Phase 6 does NOT add helpers.

Per the audit discipline carried in CLAUDE.md: "Load-bearing infrastructure (helpers, shim subsystems) is designed in the kickoff prompt or preflight, not the brainstorm." Phase 6 brainstorm respects this — no helper design happens here.

## Compiler-rt helper coverage

Phase 5 baseline `Hemispheres.raw.o` has 18 unresolved symbols (NT firmware + `__aeabi_ldivmod` + `logf` + `memcpy`/`memmove`/`memset`/`strlen` + GOT). Phase 6 adds quantizer code (likely uses `__aeabi_idivmod`, `__aeabi_uidiv`), lorenz code (float math: `__aeabi_dadd`, `__aeabi_dmul`, `__aeabi_d2lz`, possibly `__aeabi_dcmp`), and vec-osc code (already exercised by Phase 5 dep test). Coverage probe runs after Layer 0 commits and before Layer 1 dispatch.

If new EABI helpers surface beyond Phase 5's vendored set (`divdi3`, `udivdi3`, `divmoddi4`, `udivmoddi4`, `aeabi_div0`, `aeabi_ldivmod`, `aeabi_uldivmod`), Layer 0 extends `COMPILER_RT_SOURCES` in the Makefile with the corresponding files from `llvm-project/compiler-rt/lib/builtins/` at tag `llvmorg-19.1.0`. This is parent agent work; implementer tasks do not touch the Makefile or compiler-rt.

## Load-bearing decisions (recorded, not deferred)

1. **EbbAndLfo demoted from Phase 6**. Cost-benefit: 5 integration touchpoints for 1 applet vs 4 touchpoints for 14 quantizer applets (PhzIcons stubs). EbbAndLfo is the only tideslite-using applet at Phase 5 surface; demoting it costs zero Class A breadth (still 7 Class A applets land).
2. **WTVCO and DuoTET demoted from Phase 6**. Both need a CMSIS-DSP dep port. That dep is large enough to be its own phase. Phase 6 doesn't gate on it.
3. **Combin8 retained in Class A**. Adding `gfxDisplayInputMapEditor` to shim `HemisphereApplet.h` is bounded (~10 LOC). Layer 2 owns the addition. Combin8 is the only cv-map-using applet in Phase 6; demoting it would leave dep-cv-map without an applet user this phase.
4. **EnigmaJr retained in Class B**. The `DMAMEM` shim addition is a 1-line `#define` relocation, not a structural change. Layer 2 owns it. EnigmaJr is the only Turing-machine-using applet; demoting it would lose a unique applet flavor.
5. **ResetClock retained in Class C with 5-fact spec inheritance**. The Phase 4 abort report is authoritative for ResetClock semantics; the spec entry must cite all five facts. Other Class C applets (Shuffle, Xfader, Scope) have simpler clock-driven state and follow the Class C recipe without exceptions.
6. **25 applets in single Layer 1 fan-out, no sub-phases**. Wallclock target: slowest single applet (Relabi at 579 lines) plus Layer 0 prep + integration. Fan-out budget: 8 hours hard cap per kickoff.
7. **No new dep ports, no new shim subsystems**. Phase 5 froze the dep surface; Phase 6 respects the freeze. Layer 2 integration adds bounded shim base class extensions (`gfxDisplayInputMapEditor`, base for Combin8 only) and macro relocations (`DMAMEM`) — these are NOT dep extensions per the kickoff's definition of dep surface (deps live under `shim/include/<area>/`).

## Spec + Plan handoff

Spec authors per-applet entries inheriting recipe sections from kickoff verbatim. Plan structure: DAG with Layer 0 (sequential, parent) + Layer 0.5 (sequential, parent) + Layer 1 (parallel, 25 implementer subagents in one message) + Layer 2 (sequential, integration) + Layer 3 (verification). Implementer worktrees branch from `dr/phase6-applets-plan`. Pre-commit hook enforces forbidden surface.
