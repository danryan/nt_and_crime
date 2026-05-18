# Phase 5 (pivoted): Vendor dependency port batch (autonomous)

Phase 4 shipped 7 Hemisphere applet ports on `main` at squash commit `c68d6bc`. The original Phase 5 kickoff at `docs/superpowers/prompts/2026-05-18-phase5-kickoff.md` was superseded by this prompt after a preflight audit found the cat-C applet inventory at the current shim surface is exhausted. Only 4 cat-C applets (ResetClock, Shuffle, Xfader, Scope) fit without pulling deferred vendor-class dependencies, below the abort budget's 5-applet floor.

Two vendor subsystems gate roughly two-thirds of the remaining unported applet inventory: the VectorOscillator stack (Vector* family + Relabi + drum synths) and the Quantizer subsystem (every quantizer-touching applet, ~14 of the unported headers). Five smaller deps each gate 1-2 applets. The pivot ships all seven deps in parallel as Phase 5, then Phase 6 completes the applet inventory in one fan-out.

This is a vendor-shim port phase, not an applet port phase. No applet ports land in Phase 5 except the existing 31 which must not regress.

## Phase 5 scope

Ship seven vendor-dependency ports plus the time-injection harness helper plus shared Layer 0 macro infrastructure. Total target: 7 dep ports + 1 helper + macro shim. Each dep ports its vendor source files verbatim into a new shim subdirectory (or alongside existing shim headers) with a minimal wrapper plus an isolated unit test.

The bounded scope is deliberate. Phase 4 inlined Layer 1+2 as a parent-agent workflow. Phase 5 returns to the parallel implementer pattern because dep ports are genuinely independent (different vendor paths, different shim files, different math surfaces) and the wallclock target benefits from real parallelism.

## The two most important rules

These two rules are repeated because the Phase 3 attempt 1 retrospective traces the entire failure to violating them:

1. PARALLELIZE INDEPENDENT WORK. Default to parallel. Each of the seven deps is genuinely independent. The plan must explicitly declare which tasks are independent and dispatch them in a single parallel batch (multiple `Agent` tool calls in one message). End-to-end wallclock target is roughly the time of the slowest single dep plus integration.

2. EVERY IMPLEMENTER WORKTREE BRANCHES FROM `dr/phase5-deps-plan`, NOT FROM `main`. The parent agent must specify the base branch explicitly: `git worktree add <path> -b <implementer-branch> dr/phase5-deps-plan`. Never rely on the default.

## Model selection

- Parent orchestrator: `opus` (this is what you are).
- Implementer subagents: `sonnet`.
- Audit/Explore subagents: `sonnet`.

## Autonomous execution contract

The agent runs through the following phases without pausing for user review except at the preflight checkpoint:

1. Audit + verification (read primary references, verify rule prerequisites, audit each dep's vendor file surface, design Layer 0 macro shim).
2. Preflight checkpoint (post a one-paragraph preflight report, proceed unless an abort condition fired during audit).
3. Phase 5 brainstorm.
4. Phase 5 spec.
5. Phase 5 plan.
6. Layer 0: shared macro shim + Arduino.h additions + time-injection helper + helper unit test.
7. Layer 1: dep implementer subagents in parallel.
8. Layer 2: integration on feature branch.
9. Layer 3: final verification (full regression: every existing applet test still passes, `make arm` clean, hardware smoke check on 3 existing applets).
10. PR open with Load-bearing decisions section.

## Branch context

Phase 5 runs on the NEW feature branch `dr/phase5-deps-plan` which is the worktree already provisioned at `.worktrees/phase5-deps-plan` (created from current `main` at `45e12f4`).

The earlier `dr/phase5-applets-plan` branch and its worktree were discarded after the pivot. No commits landed on it.

Initialize submodules in the new worktree (already done at provisioning time): `git -C .worktrees/phase5-deps-plan submodule update --init --recursive --depth=1`.

## Worktree naming

- Parent worktree: `.worktrees/phase5-deps-plan` on branch `dr/phase5-deps-plan`.
- Implementer worktrees: `.worktrees/phase5-dep-<slug>` on branch `phase5-dep/<slug>`.
- Layer 0 + Layer 2 commits land directly on `dr/phase5-deps-plan`.

## Pre-commit hook

Phase 4 plan's pre-commit hook is the template. Update branch-name patterns to accept commits on:

- `dr/phase5-deps-plan` (Layer 0, Layer 2).
- `phase5-dep/*` (Layer 1, forbidden surface enforced per-dep).

Reject commits on `main`, on any branch not derived from `dr/phase5-deps-plan`, and any `phase5-dep/*` commit that stages a file outside the dep's allowed surface. The allowed surface is per-dep (each implementer prompt names its files); the hook enforces a coarse rule: a `phase5-dep/<slug>` branch may only touch `shim/include/<slug-area>/`, `shim/src/<slug-area>/`, and `harness/tests/test_<slug>.cpp`.

## Required skills + rules

- `superpowers:using-git-worktrees`. Step zero.
- `superpowers:brainstorming`. Drives the brainstorm.
- `superpowers:writing-plans`. Drives spec + plan.
- `superpowers:subagent-driven-development`. Drives Layer 1 parallel dispatch.

Load + honor `~/.claude/rules/parallel-execution.md`. Inline the worktree-dispatch checklist into the plan so it is auditable without the personal rules file.

## The seven deps

Each dep is one Layer 1 implementer subagent. Vendor source paths are absolute under `vendor/O_C-Phazerville/software/src/`. Shim destinations are illustrative; the brainstorm + spec finalize them.

### dep-vec-osc: VectorOscillator + WaveformManager

Vendor source:

- `vector_osc/HSVectorOscillator.h` + `HSVectorOscillator.cpp`.
- `vector_osc/WaveformManager.h` + `WaveformManager.cpp`.
- Any vendor headers transitively pulled (segment table data, waveform definitions).

Surface vendor applets touch: factory (`WaveformManager::VectorOscillatorFromWaveform`, `GetNextWaveform`), instance methods (`SetFrequency(centihertz)`, `SetPhaseIncrement(uint32)`, `SetScale`, `Offset`, `Reset()`, `Reset(phase)`, `Sustain`, `Cycle`, `Start`, `Release`, `Next()`, `TotalTime`, `GetSegment`, `SegmentCount`, `GetPhase`).

Unlocks: VectorLFO, VectorEG, VectorMod, VectorMorph, Relabi (with dep-relabi-mgr), plus drum/synth applets pending Phase 6 audit.

LoC budget ceiling: 500 LoC vendored + 100 LoC shim wrapper. Surface for descope if either ceiling exceeds by >50%.

### dep-relabi-mgr: RelabiManager

Vendor source:

- `HSRelabiManager.h` + any inline `.cpp` body.

Surface: cross-hemisphere bus (`Register(hem)`, `Unload(hem)`, `IsLinked()`, `WriteValues/ReadValues`, `WriteGates/ReadGates`).

Unlocks: Relabi (with dep-vec-osc).

LoC budget: 100 LoC. Small, may merge with dep-vec-osc implementer if the brainstorm decides bundling is cleaner.

### dep-lorenz: LorenzGeneratorManager + streams_lorenz_generator

Vendor source:

- `HSLorenzGeneratorManager.h`.
- `streams_lorenz_generator.h` + `.cc`.
- Required macros (`SCALE8_16`, `USAT16`) from `util/util_macros.h` (lands in Layer 0 shared shim).

Surface: singleton `LorenzGeneratorManager` with two `LorenzGenerator` instances; methods `SetFreq(hem, freq)`, `SetRho(hem, rho)`, `Reset(hem)`, `Process()`, `GetOut(channel)`.

Unlocks: LowerRenz.

LoC budget: 400 LoC vendored + 50 LoC singleton wrapper.

### dep-tideslite: tideslite + PhaseExtractor

Vendor source:

- `tideslite.h` + any `.cc` body.
- `util/util_phase_extractor.h` (templated header-only).
- Transitive math headers (stmlib-compat flavored).

Surface: `ComputePhaseIncrement(pitch)`, `ComputePitch(phase_inc)`, `ProcessSample(slope, shape, fold, phase, &out)`, `TidesLiteSample` struct, `PhaseExtractor<>` template.

Unlocks: EbbAndLfo (likely), WTVCO (audit).

LoC budget: 500 LoC vendored.

### dep-clock-mgr: ClockManager (full)

Vendor source:

- `HSClockManager.h` (full vendor version; current shim has a 3-method stub).
- Any `.cc` body.

Surface (current shim provides 3 of these as stubs returning 0/false; rest are vendor methods the port must provide):

- State queries: `IsRunning()`, `Tock(ch)`, `Cycle(hem)`, `EndOfBeat(hem)`.
- Public fields: `tempo`, `shuffle`.
- Control: `SetTempoBPM(bpm)`, `GetTempo()`, `SetMultiply(steps, ch)`, `GetMultiply(ch)`, `Modulate(tempo_cv, swing_cv)`.
- `ClockCycleTicks(ch)` already exists in HemisphereApplet base; verify it reads from clock_m correctly after the port.

The ClockManager runs an internal tick advance. Layer 0 hooks it into `hemispheres_shim.h` step() prologue: every inner-tick iteration in the existing `for (int i = 0; i < ticks_this_step; ++i)` loop advances clock_m by 1 tick alongside `OC::CORE::ticks += 1`. This is the load-bearing integration change; document it in spec under shim prereqs.

Unlocks: Metronome immediately; long-term unblocks any clock-aware applet (ClockSetup variants in Phase 6+).

LoC budget: 300 LoC vendored.

### dep-quant: Quantizer subsystem

Vendor source:

- `braids_quantizer.h` + `braids_quantizer_scales.h` + any `.cc`.
- `OC_scales.h` + `OC_scales.cpp` (scale tables; ~30 named scales).
- `MIDIQuantizer.h` (semitone math).
- `HS::Quantize`, `HS::QuantizerLookup`, `HS::QuantizerEdit`, `HS::GetScale`, `HS::SetScale`, `HS::GetRootNote`, `HS::GetLatestNoteNumber`, `HS::QUANT_CHANNEL_COUNT` (wherever these are defined; likely `HSUtils.h` or a vendor `HS` namespace header).
- `OC::scale_names_short`, `OC::Strings::note_names_unpadded`, `OC::Scales::GetScale`.

Surface size: this is the biggest dep. ~1000-1500 LoC across vendor files. The shim wrapper that exposes `HS::Quantize` etc. as static methods backed by a per-applet quantizer pool may add 100-200 lines.

Cross-cutting concern: some quantizer methods are exposed on the `HemisphereApplet` base class (e.g., `Quantize()`). The base class shim header at `shim/include/HemisphereApplet.h` will need a small modification to delegate to the real quantizer pool. This is shared-surface; the parent agent must sequence this edit before Layer 1 dispatch OR include the base class header in dep-quant's allowed surface explicitly.

Unlocks: 8+ applets (Pigeons, Strum, Shredder, Carpeggio, Squanch, Chordinator, DualQuant, EnigmaJr, OffsetQuant, MultiScale, ScaleDuet, DuoTET, EnsOscKey, Calibr8).

LoC budget ceiling: 1500 LoC vendored + 200 LoC shim glue. If the implementer estimates exceed this by >50%, the brainstorm must consider splitting dep-quant into Phase 5a (everything else) + Phase 5b (quantizer alone). Decide in the brainstorm; do not split mid-Layer-1.

### dep-cv-map: CVInputMap

Vendor source:

- `HSCVInputMap.h` + any `.cc` body.

Surface: per-applet table mapping logical CV input slots to physical channels; methods `GetCV(slot)`, `SetMap(slot, channel)`, etc.

Unlocks: Combin8.

LoC budget: 100 LoC.

## Time-injection helper (parent agent, Layer 0)

The helper still lands in Phase 5 because it's small, isolated, and ready for Phase 6 cat-C applet ports. The API is frozen here (not a brainstorm output) so brainstorm + spec operate against a known helper:

- API: `step_n_inner_ticks(loaded, alg, bus, N)` in `harness/tests/applet_test_helpers.{h,cpp}`.
- Mechanism: file-scope `int hem_shim::inner_ticks_override` consumed-and-cleared by `step()`. If >0, `ticks_this_step = override`; else fallback to `numFrames/3`.
- Honors 10x rule: `clocked[]` and `gate_high[]` set once per step() prologue, held across all N inner ticks. `frame.clock_countdown[]` decrements N times. `OC::CORE::ticks` advances N times.

The helper unit test must satisfy BOTH invariants. A correct-but-useless helper passes one and fails the other:

- (a) Equivalence under existing coverage. `step_n_inner_ticks(loaded, alg, bus, 10)` must produce state byte-identical to `step_n_frames(loaded, alg, bus, 32)` for Cumulus (the existing covered probe). Proves the helper is consistent with the established step() path at the default tick budget.
- (b) Tick-advancement invariant under load. `step_n_inner_ticks(loaded, alg, bus, 1000)` must advance `OC::CORE::ticks` by exactly 1000, hold `clocked[ch]` true for all 1000 inner ticks when the input gate is held high (via `hold_gate`), and decrement `frame.clock_countdown[ch]` by exactly 1000 if pre-loaded to >=1000. This is a synthetic test against the Empty applet plus a held gate input; it does not require any Clock-driven applet to exist yet. Catches the failure mode of a helper that passes (a) on a non-Clock-driven applet (LFO phase accumulator) while silently breaking on a Clock-driven applet at high tick budgets.

Both (a) and (b) must be green before Phase 5 considers Layer 0 done. Phase 6 then exercises the helper end-to-end against ResetClock (the natural Clock+time-driven probe); Phase 5 cannot use ResetClock for this purpose because no applet ports land in Phase 5.

The helper lands as part of Layer 0, before any dep implementer dispatch.

## Layer 0 shared shim infrastructure (parent agent)

Lands before Layer 1 dispatch. The shared additions are referenced by multiple deps and must exist before any implementer reads them.

1. `shim/include/util/util_macros.h`: `SCALE8_16`, `USAT16`, and any other shared vendor math macros pulled by Lorenz, VectorOsc, or Quantizer.
2. `shim/include/Arduino.h` extensions: free-function `millis()` paralleling existing `micros()`. `elapsedMillis` class (Arduino idiom for tick-delta tracking; used by VectorLFO, Tuner, others). Both back onto `OC::CORE::ticks`.
3. Time-injection helper + Cumulus probe test.
4. Section markers in `harness/tests/test_hemispheres.cpp` (per-dep regions reserved for Phase 6 applet tests; Phase 5 itself adds no applet tests, only the helper probe and per-dep isolated unit tests).
5. `harness/tests/test_dep_<slug>.cpp` skeleton files for each dep (one Catch2 file per dep, included in the existing host test build via Makefile additions).
6. Pre-commit hook update accepting `phase5-dep/*` branches.

## Operational dep-port boundary

The brainstorm MUST define the dep-port boundary with concrete predicates. Draft predicates:

1. Vendor unmodified rule: dep ports vendor source files verbatim. If the vendor header does not compile against the shim, the SHIM ADAPTS, not the vendor. Same rule as applet ports.
2. Isolated unit test required: each dep ships at least one Catch2 test exercising a known-input/known-output vendor invariant (e.g., VectorOscillator at fixed waveform + phase increment produces known sample sequence; ClockManager at BPM=120 fires Tock(0) every N ticks; Quantizer chromatic scale round-trips on input pitches).
3. Byte-parity expectation: any numerical dep (VectorOsc, Lorenz, tideslite, Quantizer) must produce identical output bytes on host (`clang++` x86) and ARM (`arm-none-eabi-c++`) for the same input. The Layer 1 unit test captures expected bytes; CI runs only the host side, but a hardware smoke check in Layer 3 verifies an existing applet still works.
4. No applet ports: Phase 5 adds no new entry to `applet_indices.h`, `HemispheresFactory.h`, `PhzIcons.h`. The existing 31 applets must continue compiling and passing.

## Phase 5 in-scope candidates (audit confirms which fit)

The 7 deps named above. The audit re-reads each vendor file end-to-end to confirm fit. Each dep audit produces:

- Vendor file list with line counts.
- External vendor dependencies (which deps depend on which other deps).
- Required Layer 0 shared additions (macros, Arduino stubs).
- Estimated shim wrapper LoC.
- Test invariant proposal (one short paragraph).

If any dep audit reveals nontrivial vendor edits would be required to compile against the shim, surface that finding before brainstorm; either the shim has a missing abstraction (fix it in Layer 0) or the dep needs descope/split.

## Out of scope (defer to Phase 6+)

- All applet ports. Phase 6 ships the applet inventory unblocked by Phase 5's deps.
- The time-injection helper itself lands in Phase 5 but no cat-C applet uses it until Phase 6.
- New hemisphere bus features (pair-applet variants, audio side, etc.).
- Vendor SDK changes (`vendor/distingNT_API` stays at `cd12d876`).
- The vendor commit `7800d929` pin stays.

## Phase 6 disciplines (carry-forward, not Phase 5 scope)

When Phase 6 audits cat-C applets against the now-shipped deps + helper, inherit these disciplines (lessons banked from the Phase 5 pivot audit):

- Helper-design as discrete preflight deliverable, not brainstorm output. Phase 5's helper API was frozen in this kickoff prompt; Phase 6 applet-port prompts must do the same for any new infrastructure they introduce. Brainstorms categorize and select, they do not design infrastructure under deadline.
- Tighter cat-C demotion abort threshold. Phase 5's superseded kickoff used "more than 3 of 7-9 candidates demote" which was too generous and concealed the cat-C inventory exhaustion until late in audit. Phase 6 should use "more than 2 of N demote, halt and assess whether the boundary is wrong or the inventory is small." A high demotion rate is evidence the boundary is mis-drawn, not that the scope is too small.
- ResetClock is the natural first cat-C applet in Phase 6 because it exercises both Clock-driven state evolution and `OC::CORE::ticks` advancement. The helper's end-to-end correctness proof happens there. Other cat-C applets follow Phase 6's standard parallel implementer fan-out pattern.
- Cat-C boundary revision is the expected outcome of a failed audit, not phase abort. If Phase 6 finds the cat-C inventory at Phase-5-extended-shim is again thin, the response is to refine boundary predicates and re-audit, not to halt or descope. The deps shipped in Phase 5 substantially change what fits.

## Vendor pin

Record the current pinned SHA of `vendor/O_C-Phazerville` at the top of the brainstorm. Phase 4 pinned at `7800d929`. Re-audit dep inventory if changed.

## Existing ports + shim are the primary reference

Read these files in full before drafting anything:

- `applets/Hemispheres.cpp`. Registration macro + test seam.
- `shim/include/applet_indices.h`. Enum (31 real applets + Empty).
- `shim/include/HemispheresFactory.h`. Registration table.
- `shim/include/HemisphereApplet.h`. Base class (shared surface, especially relevant for dep-quant).
- `shim/include/HSUtils.h`. Hemisphere utilities + macros.
- `shim/include/HSClockManager.h`. Current 3-method stub; dep-clock-mgr replaces this.
- `shim/include/Arduino.h`. Layer 0 extends this.
- `shim/include/hemispheres_shim.h`. The step() loop; Layer 0 + dep-clock-mgr both modify the inner-tick loop.
- `shim/include/OC_core.h`. `OC::CORE::ticks` declaration.
- `shim/include/util/util_math.h`. Free-function `Proportion` (Phase 4 addition); Layer 0 adds `util/util_macros.h` alongside.
- `harness/tests/applet_test_helpers.{h,cpp}`. Pack helpers + `step_n_frames`; Layer 0 adds `step_n_inner_ticks`.
- `harness/tests/test_hemispheres.cpp`. All existing TEST_CASEs (155 cases at baseline).

## Preflight report

After audit, post a preflight report with:

- Pinned `vendor/O_C-Phazerville` SHA (confirm `7800d929` or state new SHA).
- `~/.claude/rules/parallel-execution.md` status.
- Already-ported applet count (read from factory table; baseline 31 real applets + Empty).
- Phase 4 shim additions reverified.
- Layer 0 design choice (macro shim path + Arduino.h extensions + helper API).
- Per-dep audit: each of the 7 deps gets a one-line status (fits cleanly / needs N additional vendor headers / would require X LoC adjustment).
- Test baseline: 155 cases / 1803 assertions at `45e12f4`.
- Confirm `make test-applets` + `make arm` pass on `dr/phase5-deps-plan` at branch creation.

## Brainstorm requirements

- Vendor pin at top.
- Per-dep: vendor file list, surface vendor applets touch, internal vendor dependencies (which deps depend on others), LoC estimate, test invariant proposal.
- Layer 0 design (macro shim, Arduino.h extensions, helper API).
- Cross-dep concerns (HemisphereApplet base class shared surface; clock_m step() integration; util_macros shared by Lorenz+VectorOsc+Quantizer).
- Quantizer split decision (Phase 5 monolithic vs. Phase 5a/5b split). Decide here; do not defer.
- Phase 6 carry-forward inventory (which applets each dep unblocks).

## Spec requirements

- Inherit Phase 4 recipe section + section markers convention.
- New "Vendor dep port recipe" subsection. Document the standard shape: vendor source verbatim under `shim/include/<area>/` (or `shim/src/` if `.cpp`), one minimal wrapper header if surface needs adapting, one Catch2 test file at `harness/tests/test_dep_<slug>.cpp` with known-input/known-output invariants.
- Per-dep entries: vendor file path + lines, surface, Status, test invariant, shim prereqs, LoC, Phase 6 unblock list.
- Layer 0 entry: every shared addition with `file:line` citation if extending existing files.
- Spec footer: Recipe spot-check (3 claims) + Per-dep verification (3 deps; if more than 1 of 3 wrong, audit every entry) + Shared prereq verification (every Layer 0 addition).

## Plan requirements

- Start with Dependency declaration citing `~/.claude/rules/parallel-execution.md`.
- Worktree-dispatch checklist invocation as first step.
- Pre-commit hook with `phase5-dep/*` patterns + `dr/phase5-deps-plan` accept-list.
- DAG structure: Layer 0 (parent, sequential) / Layer 1 (7 dep implementers, parallel by default) / Layer 2 integration / Layer 3 verification.
- Each Layer 1 implementer task block names: worktree path, branch name, base branch (`dr/phase5-deps-plan`), allowed surface (paths under the dep's area), forbidden surface (everything else, especially other deps' areas and `applet_indices.h` / `HemispheresFactory.h`), vendor source list, required Layer 0 reads, test invariant to satisfy, abort triggers.
- HemisphereApplet base class touches are sequenced into Layer 0 (parent edits) OR explicitly allowed in dep-quant's surface; document the choice.
- Plan length under 600 lines (deps are bigger than applets; budget relaxed from the original 500).
- Spec coverage check table at the end.

## Hard constraints

- Plan declares parallel vs sequenced tasks explicitly.
- Each dep implementer's allowed surface is bounded and hook-enforced.
- Vendor source files land verbatim. Wrapper headers may adapt vendor surface to shim conventions but never modify vendor files.
- Layer 0 changes are parent-only.
- 10x clocked-multiplier rule still applies; dep-clock-mgr's step() integration MUST not bypass the rule.
- Helper unit test green before any other Layer 0 deliverable is considered done.
- `make test-applets` baseline (155 cases / 1803 assertions) MUST pass at end of Layer 2 against the existing 31 applets. Phase 5 adds dep tests, not applet tests.

## Lessons inherited from Phase 4

Apply these without re-deriving. CLAUDE.md at HEAD encodes them:

- Vendor compat headers usually compile against shim as-is. Check before stubbing.
- `Proportion` is a free function in `util/util_math.h`. Don't shadow it.
- Submodule init per new worktree: `git submodule update --init --recursive --depth=1`.
- Section markers + inline parent-agent workflow are an option for Layer 1+2, but Phase 5 deps are bigger and more independent; default to real parallel subagent dispatch unless the brainstorm finds a stronger reason to inline.
- Single-shot `set_gate` tests need `clear_bus` between steps.
- IOFrame `arr[4] = { -1 }` only initializes first element.

## Output paths

- Brainstorm: `docs/superpowers/brainstorms/YYYY-MM-DD-phase5-deps-brainstorm.md`.
- Spec: `docs/superpowers/specs/YYYY-MM-DD-phase5-deps-design.md`.
- Plan: `docs/superpowers/plans/YYYY-MM-DD-phase5-deps-plan.md`.

Use the date the document is first written.

## Abort budget

Surface immediately to the user + halt if:

During audit (before preflight):

- `~/.claude/rules/parallel-execution.md` missing worktree-dispatch checklist.
- Vendor SHA changed and deps moved.
- `make test-applets` or `make arm` fails on `dr/phase5-deps-plan` at branch creation.
- Any single dep's audit reveals it would require nontrivial vendor edits to compile against the shim (the shim has a missing abstraction; surface for Layer 0 expansion or descope).

During planning:

- Quantizer split decision deadlocks (no clean LoC ceiling fits monolithic; no clean dep boundary fits split).
- Two or more deps would require modifying the same vendor file (impossible by definition of vendor-unmodified rule; surface as evidence the brainstorm misread vendor source).
- HemisphereApplet base class shared-surface conflict cannot be cleanly sequenced into Layer 0.
- Plan exceeds 600 lines.

During Layer 0:

- Helper unit test (a) equivalence with Cumulus fails.
- Helper unit test (b) tick-advancement invariant fails (any of: `OC::CORE::ticks` delta off, `clocked[]` not held, `clock_countdown[]` decrement count off).
- Helper breaks any existing test in `make test-applets`.
- `make arm` fails or produces warnings.
- Any macro added to `util_macros.h` collides with an existing shim symbol.

During Layer 1:

- More than 2 implementer subagents abort substantively.
- Any implementer commits files outside the allowed surface despite the hook.
- Any implementer's unit test passes but the integration in Layer 2 reveals byte-divergence between host and ARM output for the dep (means the unit test invariant was too weak; pause to fix invariant, then continue).
- Fan-out wall clock exceeds 5 hours.

During Layer 2:

- `make test-applets` regresses any existing test (any of the 155 baseline cases or 1803 assertions).
- `make arm` fails or warns.
- Any dep's unit test passes on its own but fails when integrated alongside other deps (cross-dep symbol collision; surface root cause before continuing).

During Layer 3:

- Hardware smoke check on 3 existing applets reveals behavior mismatch vs Phase 4 baseline.

## Reporting

End-of-run message:

1. Documents produced (paths).
2. Phase 5 scope: 7 dep ports landed, Layer 0 additions, baseline regression status.
3. Layer 0 additions landed with commit SHAs.
4. Layer 1 result: success count, abort count with reasons, wall-clock.
5. Layer 2 integration: pass/fail.
6. Layer 3 final verification: host tests, `make arm`, hardware smoke check.
7. PR URL.
8. Phase 6 carry-forward inventory: every applet now unblocked.

If an abort fired, post the abort report instead.

## Success criteria

- Three documents exist at declared paths.
- Time-injection helper landed in Layer 0 with BOTH unit tests green: (a) Cumulus equivalence at N=10, (b) tick-advancement invariant at N=1000 against Empty + held gate.
- Layer 0 macro shim + Arduino.h extensions landed and used by at least 2 deps.
- All 7 deps shipped with isolated unit tests green.
- Quantizer split decision documented as a load-bearing call in the spec + PR description.
- `make test-applets` passes with no regressions against the Phase 5 branch creation baseline (155 cases / 1803 assertions).
- `make arm` clean, zero warnings.
- Hardware smoke check passes for 3 existing applets (regression check; no new behavior).
- PR open on `dr/phase5-deps-plan` with Load-bearing decisions summary including:
  - Pivot rationale (cat-C inventory exhausted at Phase 4 surface).
  - Quantizer monolithic vs split decision + reason.
  - HemisphereApplet base-class sequencing choice.
  - Layer 0 vs per-dep surface boundary for any borderline addition (e.g., where util_macros.h gets defined).

## First actions

1. Worktree already provisioned at `.worktrees/phase5-deps-plan` on branch `dr/phase5-deps-plan` from `main` at `45e12f4`. Confirm with `pwd` + `git rev-parse --abbrev-ref HEAD`.
2. Run `make test-applets` + `make arm` to confirm baseline green. Record case + assertion counts.
3. Read the primary references (especially `shim/include/hemispheres_shim.h` for the time-injection design and `shim/include/HemisphereApplet.h` for the dep-quant shared-surface concern).
4. Read Phase 4 brainstorm/spec/plan + the ResetClock abort report.
5. Verify `~/.claude/rules/parallel-execution.md` worktree-dispatch checklist is present.
6. Audit each of the 7 deps' vendor file surface. State LoC, dep-on-dep relationships, test invariant proposal.
7. Design the Layer 0 shared shim (macros, Arduino additions, helper API). Decide quantizer split.
8. Post preflight report. Proceed to brainstorm.
9. Write brainstorm, spec, plan.
10. Execute Layer 0 (macros + Arduino + helper + section markers + per-dep test skeletons + hook update).
11. Execute Layer 1 (7 dep implementers, parallel dispatch).
12. Execute Layer 2 (integration).
13. Execute Layer 3 (verification).
14. Open PR.
15. Post final report.

If an abort condition fires, halt + surface immediately.
