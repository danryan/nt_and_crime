# Plan: Phase 5 vendor dep port batch

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development to dispatch Layer 1/1.5 implementer
> subagents in a single parallel batch.

Date: 2026-05-18
Status: Active
Owner: Dan
Brainstorm: `docs/superpowers/brainstorms/2026-05-18-phase5-deps-brainstorm.md`
Spec: `docs/superpowers/specs/2026-05-18-phase5-deps-design.md`
Branch: `dr/phase5-deps-plan`
Worktree: `.worktrees/phase5-deps-plan`

## Goal

Land six vendor dep ports plus Layer 0a/0b shim infrastructure plus the
time-injection harness helper on `dr/phase5-deps-plan`. Open a PR. The 31
baseline applets must continue to pass `make test-applets` (155/1803) with no
regressions.

## Dependency declaration

This plan executes under `~/.claude/rules/parallel-execution.md`. The
worktree-dispatch checklist requires the parent agent to (a) specify the
base branch explicitly in `git worktree add`, (b) verify the spec and plan
docs are reachable inside each new worktree before subagent launch,
(c) verify submodules are initialized inside each new worktree,
(d) constrain the subagent's writable surface via pre-commit hook plus
prompt language. Subagents abort on missing prereqs rather than synthesizing
them.

Default is parallel; serialize only when a later item structurally depends
on an earlier one, items modify the same logical region incompatibly, or
overhead exceeds the parallel gain.

DAG:

- Layer 0a sequences internally (one commit per sub-task; `make test-applets`
  plus `make arm` between each).
- Layer 0b sequences after Layer 0a (both touch the inner-tick loop).
- Layer 1 (5 deps) plus Layer 1.5 (dep-quant) dispatch in a single parallel
  message after Layer 0b. dep-quant runs concurrently because its allowed
  surface (HemisphereApplet.h, HSUtils.h) is forbidden for the other deps.
- Layer 2 integration sequences after Layer 1+1.5.
- Layer 3 verification sequences after Layer 2.

End-to-end wallclock target: Layer 0a (~1.5h) + Layer 0b (~15m) + slowest
Layer 1 implementer (~1h) + Layer 2 (~30m) + Layer 3 (~30m) = ~3-4h.

## Worktree-dispatch checklist

Before each implementer dispatch (Layer 1, Layer 1.5):

1. `git worktree add <path> -b phase5-dep/<slug> dr/phase5-deps-plan`. Base
   MUST be `dr/phase5-deps-plan` AFTER Layer 0b commits.
2. `test -f <worktree>/docs/superpowers/specs/2026-05-18-phase5-deps-design.md`
   and `.../plans/2026-05-18-phase5-deps-plan.md`. Missing = abort.
3. `git -C <worktree> submodule update --init --recursive --depth=1`.
4. Install pre-commit hook at `<repo>/.git/worktrees/<wt-name>/hooks/pre-commit`,
   `chmod +x`. Hook content below.
5. State allowed and forbidden file surface in the dispatch prompt.

## Pre-commit hook

Installed at each worktree's git-common-dir hooks path. dep-quant gets an
exemption block that allows `HemisphereApplet.h` and `HSUtils.h`; other deps
have these in the forbidden list.

```sh
#!/bin/sh
set -e
branch=$(git rev-parse --abbrev-ref HEAD)
staged=$(git diff --cached --name-only)

hard_reject='shim/include/applet_indices.h shim/include/HemispheresFactory.h shim/include/PhzIcons.h'
shared_layer0='shim/include/util/util_macros.h shim/include/util/util_math.h shim/include/Arduino.h shim/include/FS.h shim/include/hemispheres_shim.h shim/src/globals.cpp shim/src/icons.cpp shim/src/graphics.cpp applets/Hemispheres.cpp Makefile harness/tests/test_hemispheres.cpp'

reject() { echo "pre-commit: refusing to commit $1: $2" >&2; exit 1; }

case "$branch" in
  dr/phase5-deps-plan) : ;;
  phase5-dep/quant)
    for f in $hard_reject; do echo "$staged" | grep -qx "$f" && reject "integration-owned file from dep branch" "$f"; done
    for f in $shared_layer0; do echo "$staged" | grep -qx "$f" && reject "Layer 0 shared file from dep branch" "$f"; done ;;
  phase5-dep/*)
    for f in $hard_reject; do echo "$staged" | grep -qx "$f" && reject "integration-owned file from dep branch" "$f"; done
    for f in $shared_layer0 shim/include/HemisphereApplet.h shim/include/HSUtils.h; do
        echo "$staged" | grep -qx "$f" && reject "Layer 0 or dep-quant-owned file from dep branch" "$f"
    done ;;
  *) echo "pre-commit: refusing to commit on branch '$branch' not in Phase 5 accept-list." >&2; exit 1 ;;
esac

if ! git merge-base --is-ancestor dr/phase5-deps-plan HEAD 2>/dev/null; then
    echo "pre-commit: branch '$branch' not derived from dr/phase5-deps-plan." >&2; exit 1
fi
exit 0
```

## Layer 0a tasks (parent, sequential on `dr/phase5-deps-plan`)

Each sub-task: edit, run `make test-applets && make arm` (must stay green +
clean), commit. Bracket-cited paths refer to file:line in the worktree.

### Task 0a-1: Vendor `util_macros.h`

`cp vendor/O_C-Phazerville/software/src/util/util_macros.h shim/include/util/util_macros.h`.
Verify byte-identical with `diff`. Commit:
`shim(util): vendor util_macros.h verbatim for Phase 5 deps`.

### Task 0a-2: Extend `util_math.h`

Append SCALE8_16 and USAT16 after `Proportion` (line 18). Use `#ifndef`
guards. Bodies mirror vendor `util/util_math.h:164-170`:

```cpp
#ifndef SCALE8_16
#define SCALE8_16(x) ((((x + 1) << 16) >> 8) - 1)
#endif
#ifndef USAT16
#define USAT16(x) ((x) > 65535 ? 65535 : ((x) < 0 ? 0 : (x)))
#endif
```

Commit: `shim(util): add SCALE8_16 and USAT16 macros`.

### Task 0a-3: Extend `Arduino.h`

Append after `micros()` (line 29):

```cpp
inline uint32_t millis() { return (uint32_t)(OC::CORE::ticks / 1000); }

class elapsedMillis {
public:
    elapsedMillis() : base_(millis()) {}
    elapsedMillis(uint32_t v) : base_(millis() - v) {}
    operator uint32_t() const { return millis() - base_; }
    elapsedMillis& operator=(uint32_t v) { base_ = millis() - v; return *this; }
    elapsedMillis& operator+=(uint32_t v) { base_ -= v; return *this; }
    elapsedMillis& operator-=(uint32_t v) { base_ += v; return *this; }
private:
    uint32_t base_;
};
```

Commit: `shim(Arduino): add millis() and elapsedMillis for HSClockManager`.

### Task 0a-4: `FS.h` stub

Create `shim/include/FS.h`:

```cpp
#pragma once
class File {};
class FS {};
```

Commit: `shim(FS): minimal stub for vendor OC_scales.h SD-card paths`.

### Task 0a-5: Audit `OC_DAC.h` for `kOctaveZero`

`grep -n kOctaveZero shim/include/OC_DAC.h`. If missing, add inside
`namespace OC::DAC`: `static constexpr uint8_t kOctaveZero = 5;`. Commit
only if modified: `shim(OC_DAC): add kOctaveZero constant`.

### Task 0a-6: Time-injection helper + override-global

Decl in `applet_test_helpers.h` after line 69:

```cpp
void step_n_inner_ticks(nt::LoadedPlugin* loaded, _NT_algorithm* alg,
                        float* bus, int N);
```

Def in `applet_test_helpers.cpp` after line 70:

```cpp
namespace hem_shim { extern int inner_ticks_override; }

void step_n_inner_ticks(nt::LoadedPlugin* loaded, _NT_algorithm* alg,
                        float* bus, int N) {
    hem_shim::inner_ticks_override = N;
    loaded->factory->step(alg, bus, 8);
}
```

Add `extern int inner_ticks_override;` inside `namespace hem_shim` near
top of `hemispheres_shim.h` (before the `enum { kHemSelLeft, ... }`).
Replace `int ticks_this_step = numFrames / 3;` block (lines 169-170) with:

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

Define once in `shim/src/globals.cpp`:
`namespace hem_shim { int inner_ticks_override = 0; }`. Commit:
`harness: add step_n_inner_ticks helper with override-global plumbing`.

### Task 0a-7: Helper unit tests

Add to `test_hemispheres.cpp` between section markers
`// === BEGIN helper ===` / `// === END helper ===`:

- (a) Cumulus equivalence: load Cumulus, run
  `step_n_inner_ticks(.., 10)` from state S0, capture OnDataRequest().
  Reload, run `step_n_frames(.., 32)`, capture. Assert equal.
- (b) Empty + held-gate: load Empty, `hold_gate(bus, LEFT, 0, 8)`,
  `HS::frame.clock_countdown[0] = 1000`, capture `OC::CORE::ticks` baseline
  T0, `step_n_inner_ticks(.., 1000)`, assert delta exactly 1000,
  `clock_countdown[0] == 0`, `clocked[0] == true`.

Both MUST pass before any other Layer 0 deliverable considered done. Commit:
`harness: time-injection helper unit tests`.

### Task 0a-8: Section markers + per-dep test skeletons

Append Phase 6 applet section markers to `test_hemispheres.cpp` (one
`// === BEGIN <slug> ===` / `// === END <slug> ===` block per applet:
vector_lfo, vector_eg, vector_mod, vector_morph, relabi, lower_renz,
ebb_and_lfo, wtvco, metronome, pigeons, strum, shredder, carpeggio,
squanch, chordinator, dual_quant, enigma_jr, offset_quant, multi_scale,
scale_duet, duo_tet, ens_osc_key, calibr8, combin8).

Create 6 placeholder `test_dep_<slug>.cpp` (vec_osc, lorenz, tideslite,
clock_mgr, quant, cv_map). Each starts with parity-class comment
(`// Output-parity class: <integer-only|float-tolerance|mixed>`) followed
by one placeholder `TEST_CASE("dep-<slug> placeholder", "[dep-<slug>]") {
CHECK(true); }`.

Extend `Makefile` to build per-dep Catch2 binaries
(`build/host/test_dep_<slug>`) and a `test-deps` target that runs all 6.
Pattern follows the existing `test-applets` rule.

Commit:
`harness: section markers for Phase 6 applets plus 6 dep-test skeletons`.

### Task 0a-9: Update pre-commit hook

Install hook content above at the parent worktree's
`.git/worktrees/phase5-deps-plan/hooks/pre-commit` with `chmod +x`.
Verify by creating throwaway `phase5-dep/hook-test` branch, staging
`shim/include/applet_indices.h`, attempting commit (must fail with
"refusing to commit integration-owned file"). Discard throwaway branch
afterward.

Not committed (lives in git common dir).

## Layer 0b task (parent, sequential after Layer 0a)

### Task 0b-1: ClockManager step() prologue hook

Add to `shim/include/HSClockManager.h` (existing 3-method stub class) an
`advance_one_tick` no-op stub method that the full vendor port will
later override:

```cpp
void advance_one_tick() {}
```

Add in `hemispheres_shim.h` inner-tick loop, immediately after
`OC::CORE::ticks += 1;`:

```cpp
clock_m.advance_one_tick();
```

`make test-applets` (157+ cases must still pass; the helper test (b) must
still pass; the stub doesn't interfere) + `make arm` clean. Commit:
`shim(clock): hook clock_m.advance_one_tick into inner-tick loop`.

## Layer 1 + 1.5: parallel dispatch in ONE message

Six implementer subagents dispatched together via
`superpowers:subagent-driven-development`. Each block names worktree,
branch, base branch, allowed surface, forbidden surface, vendor source,
Layer 0 reads, test invariant, abort triggers. Forbidden surface is
hook-enforced. Vendor unmodified rule applies to every implementer.

### Task 1-1: dep-vec-osc

- Worktree: `.worktrees/phase5-dep-vec-osc`, branch
  `phase5-dep/vec-osc`, base `dr/phase5-deps-plan`.
- Allowed: `shim/include/vector_osc/*.h`, `shim/include/HSRelabiManager.h`,
  `shim/src/vector_osc/*.cpp`, `harness/tests/test_dep_vec_osc.cpp`.
- Forbidden: everything else.
- Vendor verbatim: `vector_osc/HSVectorOscillator.h`,
  `vector_osc/WaveformManager.h`, `vector_osc/waveform_library.h`,
  `HSRelabiManager.h` (all under
  `vendor/O_C-Phazerville/software/src/`).
- Layer 0a reads: `shim/include/util/util_math.h`.
- Test invariant (float-tolerance, 1-LSB): build SINE
  `VectorOscillator`, `SetPhaseIncrement(0x10000)`, `Next()` 256 times,
  assert first 16 samples within 1 LSB of pre-computed expected. For
  RelabiManager: register hem 0+1, `WriteValues`/`WriteGates`, read back
  byte-identical.
- Abort: vendor edits would be required (means shim missing
  abstraction; surface for Layer 0 expansion); test invariant fails
  despite seed pin (halt).

### Task 1-2: dep-lorenz

- Worktree: `.worktrees/phase5-dep-lorenz`, branch
  `phase5-dep/lorenz`, base `dr/phase5-deps-plan`.
- Allowed: `shim/include/HSLorenzGeneratorManager.h`,
  `shim/include/lorenz/*.h`, `shim/src/lorenz/*.cpp`,
  `harness/tests/test_dep_lorenz.cpp`.
- Forbidden: everything else.
- Vendor verbatim: `HSLorenzGeneratorManager.h`,
  `streams_lorenz_generator.{h,cpp}`, `streams_resources.{h,cpp}`.
- Layer 0a reads: `shim/include/util/util_macros.h`.
- Test invariant (integer-only, byte-identical):
  `SetFreq(0,128); SetRho(0,80); Reset(0); Process()` x 1024. Assert
  `GetOut(0)` first 8 samples byte-identical to pre-computed expected.
- Abort: streams_resources duplicate-extern or link failure (diagnose
  before stripping); vendor edits required.

### Task 1-3: dep-tideslite

- Worktree: `.worktrees/phase5-dep-tideslite`, branch
  `phase5-dep/tideslite`, base `dr/phase5-deps-plan`.
- Allowed: `shim/include/tideslite/*.h`,
  `shim/include/util/util_phase_extractor.h`,
  `shim/include/util/util_pattern_predictor.h`,
  `harness/tests/test_dep_tideslite.cpp`.
- Forbidden: everything else.
- Vendor verbatim: `tideslite.h`, `util/util_phase_extractor.h`,
  `util/util_pattern_predictor.h`.
- Layer 0a reads: none beyond `<stdint.h>`.
- Test invariant (mixed: integer phase, float-tolerance sample):
  `ComputePhaseIncrement(0)` integer-equals pre-computed C4 value;
  `ProcessSample(0.5,0.5,0.0,0.25,out)` returns `out.unipolar`/
  `out.bipolar` within 1 LSB. PhaseExtractor: 4-step clock pattern,
  per-tick phase output exact fraction.
- Abort: vendor edits required.

### Task 1-4: dep-clock-mgr

- Worktree: `.worktrees/phase5-dep-clock-mgr`, branch
  `phase5-dep/clock-mgr`, base `dr/phase5-deps-plan`.
- Allowed: `shim/include/HSClockManager.h` (replaces both the existing
  3-method stub and Layer 0b's no-op `advance_one_tick`),
  `shim/include/clock/*.h`, `harness/tests/test_dep_clock_mgr.cpp`.
- Forbidden: everything else, notably `hemispheres_shim.h` (Layer 0b
  owns the call site).
- Vendor verbatim: `HSClockManager.h`. Optional `HSMIDI.h` subset (per
  spec dep entry: implementer decides whether to vendor the full file
  or stub the MIDI-IO globals).
- Layer 0a reads: `shim/include/Arduino.h`, `shim/include/OC_core.h`.
- Test invariant (mixed): `SetTempoBPM(120)`, drive
  `advance_one_tick()` N times equivalent to one beat at 120 BPM,
  assert `IsRunning()` true, `Tock(0)` rises at expected boundary,
  `GetTempo()` returns 120.
- Abort: STL header (`<functional>`, `<vector>`) fails under
  arm-none-eabi-c++ (halt, do NOT strip STL); the full vendor doesn't
  provide `advance_one_tick` (resolve via wrapper inside
  HSClockManager.h, never edit hemispheres_shim.h).

### Task 1-5: dep-cv-map

- Worktree: `.worktrees/phase5-dep-cv-map`, branch
  `phase5-dep/cv-map`, base `dr/phase5-deps-plan`.
- Allowed: `shim/include/CVInputMap.h` (replaces 20-LoC stub with
  vendor full), `shim/include/cv_map/*.h`, `shim/src/cv_map/*.cpp`,
  `shim/include/util/clkdivmult.h`,
  `harness/tests/test_dep_cv_map.cpp`.
- Forbidden: everything else. CRITICAL: do NOT edit
  `HemisphereApplet.h` even though it `#include`s `CVInputMap.h`. The
  vendor surface must preserve `cvmap[ch].In()` and
  `trigmap[ch].Gate()` (the existing call sites at
  `HemisphereApplet.h:52,61`). If preservation needs adaptation, wrap
  inside `CVInputMap.h`.
- Vendor verbatim: `CVInputMap.h`, `bjorklund.{h,cpp}`,
  `util/clkdivmult.h`.
- Layer 0a reads: none beyond existing `HSIOFrame.h`.
- Test invariant (integer-only): `cvmap[0].SetMap(0,0)`, write 1.0V to
  bus channel 0, assert `cvmap[0].In()` returns 1536. bjorklund: 3-of-8
  Euclidean pattern bitmask equals pre-computed vendor value.
  clkdivmult: `ClockDivider` div=2, 10 input ticks -> 5 output ticks.
- Abort: vendor CVInputMap surface change breaks
  `HemisphereApplet::In()`/`Gate()` at call sites (halt, surface to
  parent); bjorklund duplicate-extern with other deps (diagnose
  first).

### Task 1.5-1: dep-quant (MONOLITHIC)

- Worktree: `.worktrees/phase5-dep-quant`, branch
  `phase5-dep/quant`, base `dr/phase5-deps-plan`.
- Allowed (EXTENDED per dep-quant exemption in hook): `shim/include/quant/*.h`,
  `shim/src/quant/*.cpp`, `shim/include/HemisphereApplet.h`,
  `shim/include/HSUtils.h`, `harness/tests/test_dep_quant.cpp`.
- Forbidden: everything else (notably `hemispheres_shim.h`, other deps'
  areas, `applet_indices.h`, `HemispheresFactory.h`, `PhzIcons.h`).
- Vendor verbatim: `braids_quantizer.h`, `braids_quantizer_scales.h`,
  `OC_scales.h`, `OC_scales.cpp`. Extract `MIDIQuantizer` class from
  `HSMIDI.h:385-411` into new `shim/include/quant/MIDIQuantizer.h`.
- Layer 0a reads: `shim/include/util/util_macros.h`,
  `shim/include/Arduino.h`, `shim/include/FS.h`,
  `shim/include/OC_DAC.h`.
- HemisphereApplet edit: add member `Quantize(ch, cv, root,
  transpose)` redirecting to `HS::Quantize`.
- HSUtils edit: add HS:: quantizer surface (`QuantEngine` struct,
  `q_engine[QUANT_CHANNEL_COUNT]` extern, `Quantize`,
  `QuantizerLookup`, `QuantizerConfigure`, `GetScale`, `SetScale`,
  `GetRootNote`, `SetRootNote`, `NudgeRootNote`, `NudgeOctave`,
  `NudgeScale`, `QuantizerEdit`, `GetLatestNoteNumber`, plus
  `QUANT_CHANNEL_COUNT` enum value matching vendor 8).
- Test invariant (integer-only for scale lookups, float-tolerance for
  pitch math): chromatic round-trip through `braids::Quantizer` with
  `SCALE_SEMI` byte-identical; `HS::QuantizerConfigure(0, SCALE_SEMI,
  0xffff)` then `HS::Quantize(0, ONE_OCTAVE, 0, 0)` returns expected;
  `MIDIQuantizer::NoteNumber(ONE_OCTAVE)` returns midi-note one octave
  above kOctaveZero.
- Abort: HemisphereApplet edit breaks any existing applet test (halt);
  HSUtils additions collide with existing shim symbols (halt and
  re-audit); OC_scales.cpp fails against FS.h stub (expand stub
  minimally, never strip vendor).

## Layer 2: integration on `dr/phase5-deps-plan`

### Task 2-1

Cherry-pick (or merge) each `phase5-dep/*` branch in this order onto
`dr/phase5-deps-plan`:

```sh
git checkout dr/phase5-deps-plan
for slug in vec-osc lorenz tideslite clock-mgr cv-map quant; do
    git cherry-pick phase5-dep/$slug
done
```

Resolve append-region conflicts mechanically (concatenate; preserve
stable order). Build full host + arm:

```sh
make test-applets && make test-deps && make arm
```

Expected: 157+ cases / 1805+ assertions; 6 dep binaries green; arm
clean. Cross-dep symbol collision = abort per kickoff budget.

## Layer 3: verification + PR

### Task 3-1: full regression

`make clean && make test-applets && make test-deps && make arm`. Confirm
counts. Hardware smoke check on 3 baseline applets after PR open
(Cumulus, Voltage, EnvFollow) via `make deploy-sysex`.

### Task 3-2: open PR

```sh
git push -u origin dr/phase5-deps-plan
gh pr create --base main \
  --title "Phase 5: vendor dep port batch (6 deps, time-injection helper, Layer 0 shim)" \
  --body "$(cat <<'EOF'
## Summary

- Six vendor dep ports landed: dep-vec-osc (bundled with WaveformManager
  and RelabiManager), dep-lorenz, dep-tideslite, dep-clock-mgr (full
  ClockManager replacing 3-method stub), dep-quant (MONOLITHIC; braids
  quantizer + OC scales + MIDIQuantizer + HS:: glue), dep-cv-map (full
  CVInputMap replacing 20-LoC stub plus bjorklund plus clkdivmult).
- Layer 0a shared shim: util_macros.h vendored, util_math.h
  SCALE8_16/USAT16, Arduino.h millis + elapsedMillis, FS.h stub,
  OC_DAC.h kOctaveZero, time-injection helper step_n_inner_ticks plus
  override-global, pre-commit hook update.
- Layer 0b: clock_m.advance_one_tick() hooked into inner-tick loop.
- No applet ports. 31 baseline applets unchanged. Baseline 155/1803
  preserved plus 2 helper tests plus 6 dep invariant tests.

## Load-bearing decisions

- Pivot rationale: cat-C applet inventory exhausted at Phase 4 surface
  (only 4 of 7-9 candidates fit, below the 5-applet abort floor).
  Phase 6 inherits the unblocked applet inventory.
- Quantizer MONOLITHIC (vendor LoC 1010, under 1500 threshold).
- HemisphereApplet base-class sequencing: dep-quant owns the
  `Quantize(ch, cv, root, transpose)` member redirect, not Layer 0a.
- util_macros.h vendored verbatim; CONSTRAIN collision avoided via
  `#ifndef` guard.
- FS.h minimal stub for OC_scales SD-paths (dead code on NT).

## Test plan

- [x] make test-applets: 155 + 2 helper = 157+ assertions
- [x] make test-deps: 6 dep binaries green
- [x] make arm: zero warnings
- [ ] hardware smoke check on 3 baseline applets (post-PR-open)

## Phase 6 carry-forward

Applets unblocked: VectorLFO, VectorEG, VectorMod, VectorMorph, Relabi,
LowerRenz, EbbAndLfo, WTVCO, Metronome, Pigeons, Strum, Shredder,
Carpeggio, Squanch, Chordinator, DualQuant, EnigmaJr, OffsetQuant,
MultiScale, ScaleDuet, DuoTET, EnsOscKey, Calibr8, Combin8, plus the
cat-C applets using the time-injection helper.
EOF
)"
```

## Spec coverage check

| Spec section | Plan task |
|---|---|
| Vendor dep port recipe | Tasks 1-1..1-5, 1.5-1 |
| Recipe spot-check | Task 0a-1, 0a-8, per-dep parity comments |
| dep-vec-osc | Task 1-1 |
| dep-lorenz | Task 1-2 |
| dep-tideslite | Task 1-3 |
| dep-clock-mgr | Task 1-4 + 0b-1 |
| dep-quant | Task 1.5-1 |
| dep-cv-map | Task 1-5 |
| Layer 0a #1 util_macros.h | Task 0a-1 |
| Layer 0a #2 util_math.h | Task 0a-2 |
| Layer 0a #3 Arduino.h | Task 0a-3 |
| Layer 0a #4 FS.h | Task 0a-4 |
| Layer 0a #5 OC_DAC.h kOctaveZero | Task 0a-5 |
| Layer 0a #6 helper + override-global | Task 0a-6 |
| Layer 0a #7 helper unit tests | Task 0a-7 |
| Layer 0a #8 markers + skeletons + Makefile | Task 0a-8 |
| Layer 0a #9 pre-commit hook | Task 0a-9 |
| Layer 0b #1 clock_m hook | Task 0b-1 |
| Recipe/per-dep/prereq verification | Task 3-1 |

## Hard constraints (carry from kickoff)

- Plan declares parallel vs sequenced tasks explicitly (above).
- Each dep implementer's allowed surface is bounded and hook-enforced.
- Vendor source files land verbatim; wrappers may adapt but never modify
  vendor.
- Layer 0 changes are parent-only except dep-quant's
  HemisphereApplet.h + HSUtils.h edits.
- 10x clocked-multiplier rule preserved; Layer 0b hook does not bypass.
- Helper unit tests (a) and (b) green before any other Layer 0
  deliverable is considered done.
- `make test-applets` baseline (155 cases / 1803 assertions) MUST pass
  at end of Layer 2, plus 2 helper tests = 157+ minimum.
