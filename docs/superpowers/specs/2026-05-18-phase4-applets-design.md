# Design: Phase 4 Hemisphere applet ports

Date: 2026-05-18
Status: Draft
Owner: Dan
Brainstorm: `docs/superpowers/brainstorms/2026-05-18-phase4-applets-brainstorm.md`
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`

## Context

Phase 3 retry merged 10 category-A Hemisphere applet ports to `main` at squash commit `87596f4`. Phase 4 ships 7 more: 4 deferrals from Phase 3 (Binary, ShiftGate, Trending, ProbabilityDivider) and 3 new category-B applets (ADEG, ADSREG, GameOfLife). ResetClock is demoted to Phase 5 because it requires a category-C time-injection helper. Pair-applet variants remain deferred.

Phase 4 introduces a DAG plan structure (Layer 0 shim infrastructure, Layer 0.5 section markers, Layer 1 parallel implementer fan-out, Layer 2 integration, Layer 3 verification). The DAG validates the operational complexity at proven scale before Phase 5 broadens further.

## Recipe

This recipe inherits Phase 3 retry's recipe section verbatim. Phase 4 changes are explicitly noted.

### Files touched per port

Integration task (Layer 2) owns:

- `shim/include/applet_indices.h`: one `kApplet<Name>` enum value per applet, alpha-ordered, before `kAppletCount`.
- `shim/include/HemispheresFactory.h`: five entries per applet, all alpha-positioned: `#include "<Applet>.h"`; entry in `applet_enum_strings()` `names[]`; entry in `kMaxAppletSize` `cmax(...)` chain; entry in `kMaxAppletAlign` `cmax(...)` chain; entry in `applet_factory()` `table[]` at the index matching the enum order.
- `shim/include/PhzIcons.h`: one `extern const uint8_t <icon>[8];` stub per applet.

Implementer task (Layer 1) owns:

- `vendor/O_C-Phazerville/software/src/applets/<Applet>.h`: read-only vendor source.
- `harness/tests/applet_test_helpers.h`: one `uint64_t pack_<applet>(...)` declaration. Skip when the applet's `OnDataRequest()` returns 0 (Binary in this phase).
- `harness/tests/applet_test_helpers.cpp`: one `pack_<applet>` implementation. Skip when no declaration.
- `harness/tests/test_hemispheres.cpp`: `using hem_shim::kApplet<Name>;`, optional `<applet>_set(hi, ...)` helper, N `TEST_CASE`s under tag `[<applet>]`, all placed between this applet's `// === BEGIN <applet> ===` / `// === END <applet> ===` section markers.

Shim infrastructure (Layer 0) work commits to `dr/phase4-applets-plan` directly, before any Layer 1 implementer is dispatched. Layer 0 is the integration task's responsibility for Phase 4; implementers see Layer 0 results as part of the worktree's base branch state.

The implementer never commits to `shim/include/`, `shim/src/`, or any other shim infrastructure file. The pre-commit hook installed in each implementer worktree by the parent agent enforces this and rejects commits that stage forbidden-surface changes.

### Pack helper signature

Pack helpers mirror the vendor `OnDataRequest` byte-by-byte. They live in `hem_test` and follow the shape of `pack_cumulus` at `harness/tests/applet_test_helpers.cpp:163`:

```cpp
uint64_t pack_<applet>(<typed_params>) {
    uint64_t data = 0;
    data |= ((uint64_t)(<field_with_bias>) & <mask>) << <offset>;
    return data;
}
```

Rules:

- Use `int` for each field at the helper boundary; apply the vendor bias inside the helper.
- Use the same bias the vendor uses on `OnDataReceive`. If `OnDataReceive` does `field = Unpack(...) - N`, the helper writes `((uint64_t)(field + N) & mask) << offset`.
- For sub-byte fields, AND with the field-width mask, not `0xFF`.
- Explicitly zero gap bits between vendor `Pack` calls; see Cumulus bits 11..12 and Voltage bit 9.
- For applets whose `OnDataRequest()` returns `0`, declare no `pack_<applet>` helper. The round-trip case asserts `OnDataRequest() == 0` directly.

### Test setup primitive

Every test starts with `setup_applet(idx, side = LEFT)` from `harness/tests/test_hemispheres.cpp:40`:

```cpp
auto s = setup_applet(kAppletXxx);
```

`s` is `{loaded, alg, bus, hi}`. The applet's `Start()` has run by the time `setup_applet` returns.

### Bus driving

- Single-sample gate edge: `set_gate(bus, LEFT, channel, frame_offset, 8)`.
- Sustained gate: `hold_gate(bus, LEFT, channel, 8)`.
- Flat CV across the buffer: `set_cv(bus, LEFT, channel, volts, 8)`.
- Always `clear_bus(bus)` between scenarios; bus state persists across `step_n_frames` calls.
- The trailing `8` is `numFramesBy4` (32 frames per buffer).

### Stepping

`step_n_frames(loaded, alg, bus, n_samples)` issues `ceil(n_samples / 32)` `step()` calls. One `step()` runs the vendor `Controller()` exactly `ticks_this_step = numFrames / 3 = 10` times.

### Output reading

- Gate at a frame: `read_gate_at(bus, LEFT, channel, frame_offset, 8)`.
- CV at a frame: `read_cv_at(bus, LEFT, channel, frame_offset, 8)` returns volts.
- CV assertions use `Approx(target).margin(...)` with margins sized to vendor int rounding (typically 0.01-0.1V).

### RNG

Applets that call `random_word()` or `random()` route through the shim's `hem_rng_state` xorshift32 global. Tests seed reproducibly via `seed_hem_rng(0xDEADBEEF)` after `setup_applet`. Phase 4 adds `randomSeed(uint32_t)` in the shim so vendor calls reseed `hem_rng_state` deterministically; tests can either set RNG via `seed_hem_rng` directly or rely on the vendor's own `randomSeed` invocation given a known `micros()` value.

### TEST_CASE shape

Each applet ships at minimum these cases under tag `[<applet>]`:

- `Start defaults match vendor`: read `OnDataRequest()` immediately after `setup_applet`, decode each field, assert against `Start()` values. Empty-`OnDataRequest` applets assert `OnDataRequest() == 0`.
- `OnDataReceive then OnDataRequest round-trip preserves all serialised fields`: pack a non-default value inside the vendor's `constrain` ranges, call `OnDataReceive(packed)`, read `OnDataRequest()`, decode and compare. Empty-`OnDataRequest` applets repeat the defaults case.
- One case per major Controller branch. Phase 2 averaged 6 cases per applet.

Field values for the round-trip case must stay inside the vendor's `OnDataReceive` constraints.

### Bus-level behavioral assertions and the 10x clocked multiplier

The shim runs the vendor `Controller()` 10 times per `step()` call (`ticks_this_step = numFrames / 3 = 10`). A single rising edge on `Clock(side)` sets `clocked[side] = true` once, but `clocked[side]` stays asserted for all 10 inner Controller calls in that buffer. See `harness/tests/test_hemispheres.cpp:1264-1280` (Cumulus CU2) for the canonical commentary and assertion math.

Consequence: for any applet whose `Controller()` advances an internal counter, accumulator, or toggle inside `if (Clock(ch))`, one input edge produces 10 internal advances per buffer, not one.

Two valid coverage shapes apply:

1. Model the multiplier explicitly. Compute the expected post-buffer state assuming 10 inner Controller iterations. Cumulus CU2, Stairs ST2-ST3, RunglBook RB2 are the precedent.
2. Drop bus-level fire-count assertions; restrict coverage to round-trip plus state-injection. ProbabilityDivider is the precedent.

Per-applet entries that involve internal counters consuming `Clock(ch)` must state which shape they use.

### Section markers in `test_hemispheres.cpp`

Each Phase 4 in-scope applet has a pair of section markers placed by the Layer 0.5 preparatory commit on `dr/phase4-applets-plan`:

```cpp
// === BEGIN <applet> ===
// === END <applet> ===
```

Identical markers appear in `harness/tests/applet_test_helpers.h` (pack helper decl) and `harness/tests/applet_test_helpers.cpp` (pack helper impl) for applets that have a pack helper.

Each Layer 1 implementer adds content ONLY between its own markers. Implementers run `make test-applets` before commit so a compile failure surfaces a missing closing brace or content placed outside markers immediately, before the cherry-pick. This mitigates the brace-eating hazard observed in Phase 3 retry integration. The integration commit verifies brace balance the same way. The Python repair script from Phase 3 retry is retained as fallback; if section markers fail to prevent a brace imbalance at Layer 2, the script repairs and Layer 2 surfaces as an integration concern but does not halt unless the repaired tree still fails to compile.

### Integration ordering

The integration step (Layer 2) is sequenced after all implementer commits land. It cherry-picks the implementer commits onto `dr/phase4-applets-plan`, mechanically merges within section markers (each marker pair owns its slice), edits `applet_indices.h`, `HemispheresFactory.h`, `PhzIcons.h` in alphabetical order across the full Phase 4 set, then runs `make test-applets` and `make arm` for verification.

## Per-applet entries

### Binary

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Binary.h:26-104`.
- Category: A (Phase 3 deferral, treated as cat-A in Phase 4).
- Status: in scope; depends on Layer 0 shim work for `SegmentDisplay` stub + `PhzIcons::binaryCounter` icon.
- Cherry-pick: re-implement.
- `OnDataRequest` (`Binary.h:65-68`): returns 0. No serialised state. No pack helper.
- `Start()` (`Binary.h:34`): empty body. Member defaults: `bit[4] = {false, false, false, false}` (default-initialized). Constants `CVal = HEMISPHERE_MAX_CV / 4`, `B0Val = HEMISPHERE_MAX_CV / 15`.
- `Controller()` (`Binary.h:36-53`): Reads `bit[0]=Gate(0)`, `bit[1]=Gate(1)`, `bit[2]=In(0) > HEMISPHERE_3V_CV`, `bit[3]=In(1) > HEMISPHERE_3V_CV`. `Out(0) = bit[3]*B0Val + bit[2]*2*B0Val + bit[1]*4*B0Val + bit[0]*8*B0Val` (4-bit MSB-first binary counter, 0..HEMISPHERE_MAX_CV). `Out(1) = (bit[0]+bit[1]+bit[2]+bit[3]) * CVal` (count of high bits, 1V per bit).
- Test concerns: No 10x risk (pure combinational logic each Controller tick). Cases: defaults assert `OnDataRequest() == 0` and `Out(0) == 0`; CV-only inputs (Gate(0)=Gate(1)=low, In(0)=4V, In(1)=4V) assert `Out(0) = 3 * B0Val = 0.6V * 3 = 1.8V` and `Out(1) = 2 * CVal = 3V`; all-bits-high (hold Gate(0), Gate(1), In(0)=4V, In(1)=4V) assert `Out(0) = 15 * B0Val = HEMISPHERE_MAX_CV = 6V` and `Out(1) = 4 * CVal = 6V`; all-bits-low assert `Out(0) = 0V` and `Out(1) = 0V`.
- Shim prereqs: SegmentDisplay stub (View-only; render methods no-op for headless tests); icon `binaryCounter` in PhzIcons.
- Analogue: GatedVCA / Button (empty-`OnDataRequest` pattern, In/Gate-only Controller).

### ShiftGate

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ShiftGate.h:1-129`.
- Category: A (Phase 3 deferral).
- Status: in scope; depends on Layer 0 shim work for `LOOP_ICON`, `X_NOTE_ICON`, `PhzIcons::shiftGate` icon.
- Cherry-pick: re-implement (Phase 3 attempt-1 dirty archive predates current shim).
- `OnDataRequest` (`ShiftGate.h:63-71`): 32 bits used. `length[0] - 1` at `[0,4)`, `length[1] - 1` at `[4,4)`, `trigger[0]` at `[8,1)`, `trigger[1]` at `[9,1)`, `reg[0]` at `[16,16)`. Note: only `reg[0]` is serialised, not `reg[1]`.
- `Start()` (`ShiftGate.h:9-16`): `length[ch] = 4`, `trigger[ch] = ch` (so `trigger[0]=0, trigger[1]=1`), `reg[ch] = random(0, 0xffff)`.
- `Controller()` (`ShiftGate.h:18-43`): On `Clock(0)`, `StartADCLag()`. On `EndOfADCLag()`, per channel `ch`: capture `last` bit; if `!Clock(1)` (freeze inactive), `data = In(ch) > HEMISPHERE_3V_CV ? 1 : 0`, `last = (data != last)`; shift `reg[ch] = (reg[ch] << 1) + last`; if `trigger[ch] == 1`, `ClockOut(ch)` on `reg[ch] & 0x01`; else `GateOut(ch, reg[ch] & 0x01)`.
- Test concerns: 10x risk on `reg[ch]` shifts. Single `Clock(0)` buffer fires `StartADCLag()` once. `EndOfADCLag()` returns true after `HEMISPHERE_ADC_LAG=96` inner ticks; with 10 ticks per buffer, EndOfADCLag triggers ~9-10 buffers after the Clock(0) buffer. Tests: defaults assert `length[ch]=4, trigger[0]=0, trigger[1]=1`; round-trip with `length[0]=8, length[1]=12, trigger[0]=1, trigger[1]=0, reg[0]=0x1234`; behavior: seed reg via `OnDataReceive`, drive `Clock(0)` + In(0)=4V (1-bit), step >100 samples to allow ADC lag, assert `reg[0]` shifts visible via `OnDataRequest`. State-injection only for shift dynamics; bus-level "fires per N edges" not asserted.
- Shim prereqs: `LOOP_ICON`, `X_NOTE_ICON` in HSicons; `shiftGate` icon in PhzIcons.
- Analogue: RunglBook (8-bit shift register on Clock(0), `In(0) > threshold` data bit).

### Trending

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Trending.h:31-192`.
- Category: A (Phase 3 deferral).
- Status: in scope; depends on Layer 0 shim work for `HemisphereApplet::Changed(int ch)` + shim `step()` updates `changed_cv` from frame-to-frame input deltas (threshold = `HEMISPHERE_CHANGE_THRESHOLD = 32` per `vendor/HSUtils.h:25`) + `PhzIcons::trending` icon.
- Cherry-pick: re-implement.
- `OnDataRequest` (`Trending.h:109-115`): 16 bits. `assign[0]` at `[0,4)`, `assign[1]` at `[4,4)`, `sensitivity` at `[8,8)`. No bias.
- `Start()` (`Trending.h:39-48`): `assign[ch]=ch` (so `assign[0]=0, assign[1]=1`), `result[ch]=0`, `fire[ch]=0`, `sample_countdown=0`, `sensitivity=40`.
- `Controller()` (`Trending.h:50-87`): Two-phase. When `--sample_countdown < 0`, reset to `(TRENDING_MAX_SENS=124 - sensitivity) * 20`, clamped >=96. Then per channel: compute `trend = GetTrend(ch)` from `result[ch]`. If `reset[ch]`, zero Out and clear reset. If `assign[ch] < changedstate`, `GateOut(ch, gate)` where gate is true if `assign[ch] == trend` (or `assign[ch] == moving && trend != steady`). If `assign[ch] == changedstate && trend != last_trend[ch]`, `fire[ch]=1`. If `fire[ch]`, `ClockOut(ch)`. `last_trend[ch] = trend`. Else (countdown not expired): per channel `changed = Changed(ch)`; `signal[ch] = In(ch)`; `Observe(ch, signal, last_signal)` increments `result[ch]` on abs(signal-last)>10; if `assign[ch] == changedvalue && changed`, `fire[ch]=1`.
- Test concerns: 10x affected on `result[ch]` integrator (Observe runs 10x per buffer). The trend buckets (rising/falling/steady) are bounded by `result[ch]` thresholds at +/-3, so post-buffer trend determinable. Cases: defaults assert `assign[0]=0, assign[1]=1, sensitivity=40`; round-trip with `assign[0]=3, assign[1]=4, sensitivity=80`; behavior: `assign[0]=0 (rising)`, ramp `In(0)` from 0V to 4V across many buffers, assert `GateOut(0)` goes high once trend bucket flips to rising; reverse to confirm falling.
- Shim prereqs: `Changed(int ch)` method on HemisphereApplet + shim `changed_cv` update + `trending` icon.
- Analogue: EnvFollow (per-channel CV transformation with per-side parameter + sensitivity).

### ProbabilityDivider

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ProbabilityDivider.h:23-333`.
- Category: A (Phase 3 deferral).
- Status: in scope; depends on Layer 0 shim work for `LOOP_ICON` + `DrawSlider` method + `randomSeed(uint32_t)` + `micros()` + `ProbLoopLinker` stub + `PhzIcons::probDiv`.
- Cherry-pick: re-implement (Layer 0 ProbLoopLinker stub changes assumptions; rebasing archive is harder than reimplementing).
- `OnDataRequest` (`ProbabilityDivider.h:179-189`): 40 bits. `weight_1` at `[0,4)`, `weight_2` at `[4,4)`, `weight_4` at `[8,4)`, `weight_8` at `[12,4)`, `loop_length` at `[16,8)`, `loop_linker.GetSeed()` at `[24,16)`.
- `Start()` (`ProbabilityDivider.h:40-53`): all weights 0, `loop_length=0`, `loop_index=0`, `loop_step=0`, `skip_steps=0`, `bypass_loop=false`, `GateOut(ch, false)`. `MAX_WEIGHT=15`, `MAX_LOOP_LENGTH=32`.
- `Controller()` (`ProbabilityDivider.h:58-142`): On Clock(0): if `--skip_steps > 0`, advance loop and `ClockOut(1)` then early return. If `loop_length_mod > 0`, `skip_steps = GetNextLoopDiv()`. If `loop_length_mod == 0 || bypass_loop`, `skip_steps = GetNextWeightedDiv()`. If `skip_steps == 0`, early return. Else `ClockOut(0)`. CV1 modulates `loop_length_mod`. CV2 high (>2.5V) triggers reseed via `GenerateLoop(true, false)`; CV2 very low (<-2.5V) bypasses loop.
- Test concerns: 10x affected (`skip_steps` decrements per inner Controller tick under `clocked[0]=true`). Coverage shape: round-trip plus state-injection only. Cases: defaults assert all weights=0, loop_length=0; round-trip with `weight_1=15, weight_2=10, weight_4=5, weight_8=0, loop_length=8, seed=0xCAFE` (use `loop_linker.SetSeed` via the stub); state-injection verifies the four weights are honored by reading post-`OnDataReceive` `OnDataRequest()`. No bus-level fire-count assertion.
- Shim prereqs: `LOOP_ICON` (shared with ShiftGate), `DrawSlider` method, `randomSeed`/`micros` shims, `ProbLoopLinker` stub singleton, `probDiv` icon.
- Analogue: Brancher (RNG-driven) with multi-weight pack akin to Cumulus.

### ADEG

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ADEG.h:30-176`.
- Category: B.
- Status: in scope; depends on Layer 0 shim work for `PhzIcons::AD_EG` icon (defined at vendor `PhzIcons.h:61`).
- Cherry-pick: n/a (new port).
- `OnDataRequest` (`ADEG.h:64-69`): 16 bits. `attack` at `[0,8)`, `decay` at `[8,8)`. No bias.
- `Start()` (`ADEG.h:32-37`): `signal=0`, `phase=0`, `attack=50`, `decay=50`. `HEM_ADEG_MAX_VALUE=255`, `HEM_ADEG_MAX_TICKS=33333` (approx 2 seconds at 16.6kHz Controller rate; on shim this scales with `OC::CORE::ticks` budget).
- `Controller()` (`ADEG.h:40-95`): On Clock(0): trigger envelope, `phase=1` (attack), `effective_attack=attack`, `effective_decay=decay`. On Clock(1): reverse trigger, swap effective parameters. While `phase > 0`: integrate `signal` toward `target` (HEMISPHERE_MAX_CV for attack, 0 for decay) over `Proportion(segment, HEM_ADEG_MAX_VALUE, HEM_ADEG_MAX_TICKS)` ticks where `segment = effective_attack + Proportion(DetentedIn(0), HEMISPHERE_MAX_INPUT_CV, HEM_ADEG_MAX_VALUE)` for attack, similar with `DetentedIn(1)` for decay. On `signal >= HEMISPHERE_MAX_CV && phase == 1`, transition to `phase=2` (decay). On `signal <= 0 && phase == 2`, `ClockOut(1)` (EOC) and `phase=0`. `Out(0, signal)`.
- Test concerns: 10x affected on `signal` integrator. Tests model the 10x explicitly (Cumulus CU2 / Stairs ST2-ST3 precedent). Cases: defaults assert `attack=50, decay=50`; round-trip with `attack=120, decay=200`; behavior: trigger Clock(0) once with `attack=1` (fast attack), step enough buffers for attack-to-decay transition, assert `Out(0)` rises then falls; verify `ClockOut(1)` EOC fires after decay completes. Long-attack case (`attack=255`) verifies integration math under known tick budget. CV-modulation case: hold `In(0)` at 4V (DetentedIn nonzero), trigger Clock(0), assert effective attack stretches.
- Shim prereqs: `AD_EG` icon in PhzIcons.
- Analogue: Slew (single CV-out integrator with two signed params).

### ADSREG

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ADSREG.h:38-378`.
- Category: B.
- Status: in scope; depends on Layer 0 shim work for `PhzIcons::ADSR_EG` icon (defined at vendor `PhzIcons.h:60`). The `MiniADSR` envelope struct is self-contained inside the applet class.
- Cherry-pick: n/a (new port).
- `OnDataRequest` (`ADSREG.h:241-249`): 64 bits. Per channel `ch in {0,1}`: `setting[0]` (attack) at `[ch*32 + 0, 8)`, `setting[1]` (decay) at `[ch*32 + 8, 8)`, `setting[2]` (sustain) at `[ch*32 + 16, 8)`, `setting[3]` (release) at `[ch*32 + 24, 8)`. No bias. STAGE_MAX_VALUE=255.
- `Start()` (`ADSREG.h:193-198`): `cursor=0`; per channel: `adsr_env[ch].Init(ch)` sets `stage_ticks=0, gated=0, stage=NO_STAGE, setting[ATTACK]=10+ch*10, setting[DECAY]=30, setting[SUSTAIN]=120, setting[RELEASE]=25+ch*10, cv_mod=0`. So ch0: `(10,30,120,25)`; ch1: `(20,30,120,35)`.
- `Controller()` (`ADSREG.h:200-208`): Per channel: `adsr.cv_mod = get_modification_with_input(ch)` (DetentedIn-driven release modulation); `adsr.Process(Gate(ch))` advances the ADSR state machine; `Out(ch, adsr.GetAmplitude())`.
- Test concerns: 10x affected on `stage_ticks` integrator. Per `Process()` call: gate-high transitions through ATTACK -> DECAY -> SUSTAIN; gate-low triggers RELEASE then NO_STAGE. Cases: defaults assert `(10,30,120,25)` ch0 and `(20,30,120,35)` ch1 via OnDataRequest; round-trip with `(50,40,200,80)` both channels; behavior: hold gate high (`hold_gate`) for many buffers, assert `Out(0)` rises through ATTACK, levels at SUSTAIN proportional to `setting[2]`; release gate, assert decay. Use small attack/decay values (1-2) to keep tick budget per assertion reasonable.
- Shim prereqs: `ADSR_EG` icon in PhzIcons.
- Analogue: EnvFollow (envelope tracking with per-channel parameters; ADSREG packs 4 parameters per channel vs EnvFollow's 1).

### GameOfLife

- Vendor: `vendor/O_C-Phazerville/software/src/applets/GameOfLife.h`.
- Category: B (borderline; admitted by boundary because state is a closed system with no RNG and CVs drive position not steps).
- Status: in scope; depends on Layer 0 shim work for `PhzIcons::gameOfLife` icon (defined at vendor `PhzIcons.h:87`).
- Cherry-pick: n/a (new port).
- `OnDataRequest` (vendor lines): 6 bits. `weight` at `[0,6)`. No bias. Range 0..63 (6 bits, vendor doesn't constrain on receive past mask).
- `Start()` (vendor lines): `board[80] = 0`, `weight=30`, `tx=0`, `ty=0`, then a fixed-pattern initial AddToBoard sequence (X-pattern centered at 26..32, 23..28). Deterministic; no RNG.
- `Controller()` (vendor lines): `tx = ProportionCV(In(0), 63, HEMISPHERE_MAX_INPUT_CV)`; `ty = ProportionCV(In(1), 39, HEMISPHERE_MAX_INPUT_CV)`. On Clock(0): `ProcessGameBoard(tx, ty)` advances cellular automaton one generation. On Gate(1) high: `AddToBoard(tx, ty)` toggles cell at position. `global_density_cv = Proportion(global_density, 1200 - weight*10, HEMISPHERE_MAX_CV)`. `local_density_cv = Proportion(local_density, 225, HEMISPHERE_MAX_CV)`. `Out(0)` = global, `Out(1)` = local, both `constrain`-clipped.
- Test concerns: 10x risk: with `clocked[0]=true`, `ProcessGameBoard` runs 10 times per buffer; with `gate_high[1]=true`, `AddToBoard` runs 10 times per buffer (toggles cell 10 times = net no change for any specific cell). Cases: defaults assert OnDataRequest = 30 (weight=30); round-trip with `weight=45`; behavior: with fixed Start pattern, no clock fires - assert `Out(0)` reflects initial board density (non-zero) and `Out(1)` reflects local density near initial cluster; under sustained `Gate(1)` with `In(0)=2V` (tx around 32) and `In(1)=2V` (ty around 19), assert `Out(0)` and `Out(1)` change as cells toggle; under sustained Clock(0), assert generations advance (density evolves).
- Shim prereqs: `gameOfLife` icon in PhzIcons.
- Analogue: RunglBook (state machine driven by Clock(0) with CV-thresholded data, no time-based logic).

## Spec footer

### Recipe spot-check

Three load-bearing claims in the Recipe section verified against existing code:

1. **"`ticks_this_step = numFrames / 3 = 10` inner Controller calls per `step()`."** Verified at `shim/include/hemispheres_shim.h:156-168`. `numFrames = numFramesBy4 * 4 = 32`; `ticks_this_step = numFrames / 3 = 10`; clamp to 1 minimum; loop runs `Controller()` for both halves per inner tick.
2. **"Pack helpers explicitly zero gap bits between vendor `Pack` calls."** Verified at `harness/tests/applet_test_helpers.cpp:179-187` (pack_cumulus comment: `// bits 11..12 left as 0 (vendor gap)`) and `applet_test_helpers.cpp:243-251` (pack_voltage: `// bit 9 left as 0 (vendor gap between voltage[0] and voltage[1])`).
3. **"Phase 3 retry shim additions are in place at HEAD."** Verified at `shim/include/HSUtils.h:13-15` (`HEMISPHERE_3V_CV`, `HEMISPHERE_PULSE_ANIMATION_TIME`, `_LONG`), `shim/include/HSicons.h:18-22` (`CHECK_ON_ICON`, `CHECK_OFF_ICON`, `STAIRS_ICON`, `UP_BTN_ICON`, `RESET_ICON`).

### Per-entry verification

Three randomly-selected per-applet entries traced end-to-end against vendor source.

1. **Trending**. `OnDataRequest` layout (`Trending.h:109-115`): `assign[0]` at `[0,4)`, `assign[1]` at `[4,4)`, `sensitivity` at `[8,8)`. Confirmed. `Start()` (`Trending.h:39-48`): `assign[ch]=ch`, `sensitivity=40`. Confirmed. `Controller()` (`Trending.h:50-87`): two-phase (countdown gates Observe-loop vs trend-update). `Changed(ch)` is read at `Trending.h:81`. Confirmed dependency on Layer 0 `Changed(int ch)` method. Entry's claim about `result[ch]` bounded by +/-3 thresholds (lines 188-190 in `GetTrend`) confirmed. **Confirmed.**
2. **ADEG**. `OnDataRequest` layout (`ADEG.h:64-69`): `attack` at `[0,8)`, `decay` at `[8,8)`. Confirmed. `Start()` (`ADEG.h:32-37`): all five fields verified. `Controller()` (`ADEG.h:40-95`): trigger semantics on Clock(0) and Clock(1), phase machine, integrator math, EOC at `ClockOut(1)`. Confirmed. 10x risk on `signal` integrator confirmed; tests must model. **Confirmed.**
3. **Binary**. `OnDataRequest` (`Binary.h:65-68`): returns 0. Confirmed. `Start()` (`Binary.h:34`): empty body; member defaults via class initialisers. `Controller()` (`Binary.h:36-53`): bit reads from Gate and In>HEMISPHERE_3V_CV, sum output and count output. Confirmed. Note: `HEMISPHERE_3V_CV` already in shim at `HSUtils.h:13`; SegmentDisplay used only in View() (DrawDisplay), so shim stub with no-op render is sufficient for headless tests. **Confirmed.**

Result: 0 of 3 entries contradict vendor reality. Below the "more than 1 of 3" gate; no expanded audit triggered.

### Shim prereq verification

For each Layer 0 shim addition, the vendor source it mirrors and the existing shim file it lands in:

- **LOOP_ICON, X_NOTE_ICON** (HSicons.h additions): vendor source at `vendor/HSicons.h:65` (LOOP_ICON bitmap), `vendor/HSicons.h:91` (X_NOTE_ICON bitmap). Lands in `shim/include/HSicons.h` (extern decl) + `shim/src/icons.cpp` (bitmap content). Used by ShiftGate (`ShiftGate.h:104-109`), ProbabilityDivider (`ProbabilityDivider.h:258`).
- **HemisphereApplet::Changed(int ch)** (HemisphereApplet.h method): vendor source at `vendor/HemisphereApplet.h:147` (`bool Changed(int ch) {return frame.changed_cv[io_offset + ch];}`). Lands in `shim/include/HemisphereApplet.h` (method) + `shim/include/hemispheres_shim.h` (step() updates `changed_cv` using vendor's threshold `HEMISPHERE_CHANGE_THRESHOLD=32` at `vendor/HSUtils.h:25`). Used by Trending (`Trending.h:81`).
- **DrawSlider method** (HemisphereApplet.h method): vendor source at `vendor/HemisphereApplet.h:541-572` (DrawSlider signature `void DrawSlider(uint8_t x, uint8_t y, uint8_t len, uint8_t value, uint8_t max_val, bool is_cursor)`). Lands in `shim/include/HemisphereApplet.h`. View-only; no test impact. Used by ProbabilityDivider (`ProbabilityDivider.h:249`).
- **randomSeed(uint32_t)** (Arduino.h or HemisphereApplet.h): vendor source at Arduino API (no direct vendor file; standard Arduino library). Lands in `shim/include/HemisphereApplet.h` near the existing `random()` macro at line 212. Implementation: `inline void randomSeed(uint32_t seed) { hem_rng_state = seed ? seed : 0xDEADBEEFu; }`. Used by ProbabilityDivider (`ProbabilityDivider.h:301`).
- **micros()** (Arduino.h): vendor source at Arduino API. Lands in `shim/include/Arduino.h` or `shim/include/OC_core.h`. Implementation: `inline uint32_t micros() { return (uint32_t)OC::CORE::ticks; }`. Sufficient for vendor's use as RNG seed. Used by ProbabilityDivider (`ProbabilityDivider.h:301`).
- **ProbLoopLinker stub** (shim/include/HSProbLoopLinker.h): vendor source at `vendor/HSProbLoopLinker.h`. Lands in `shim/include/HSProbLoopLinker.h` (new file mirroring vendor path). Stub class with `RegisterDiv`, `UnloadDiv`, `RegisterMelo` (no-op), `UnloadMelo` (no-op), `IsLinked` returning false, `SetLooping`, `TriggerRegeneration`, `SetLoopStep`, `Trigger`, `TrigPop` returning false (no Melo linked), `GetSeed`/`SetSeed` maintaining a `uint16_t` field, singleton via `get()`. Used by ProbabilityDivider (`ProbabilityDivider.h:21,238`).
- **SegmentDisplay stub** (shim/include/SegmentDisplay.h): vendor source at `vendor/SegmentDisplay.h`. Lands in `shim/include/SegmentDisplay.h` (new file mirroring vendor path). Stub class with `enum SegmentSize`, constructor taking SegmentSize (no-op), `SetPosition(uint8_t x, uint8_t y)` (no-op), `PrintDigit(uint8_t)` (no-op). View-only. Used by Binary (`Binary.h:24,91`).
- **Icon bitmaps in PhzIcons.h + icons.cpp**: vendor sources at `vendor/PhzIcons.h:38` (shiftGate), `:45` (trending), `:52` (binaryCounter), `:55` (probDiv), `:60` (ADSR_EG), `:61` (AD_EG), `:87` (gameOfLife). Lands in `shim/include/PhzIcons.h` (extern decls) + `shim/src/icons.cpp` (bitmap content copied verbatim).
