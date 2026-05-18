# Design: Phase 3 Hemisphere applet ports

Date: 2026-05-18
Status: Draft
Owner: Dan
Brainstorm: `docs/superpowers/brainstorms/2026-05-17-phase3-applets-brainstorm.md`
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`

## Context

Phase 1 and Phase 2 ported 13 vendor Hemisphere applets plus the shim-local `Empty` into the disting NT compatibility shim, each with a host-side Catch2 test suite that drives the plug-in's NT API. Phase 3 ports a further 15 category-A applets in parallel under the same harness. No harness change, no new shim helpers.

The recipe section below is the canonical port pattern, derived from the five primary-reference files. Per-applet entries describe only the deltas from the recipe. The plan document (`docs/superpowers/plans/2026-05-18-phase3-applets-plan.md`) translates the entries into a parallel worklist.

## Recipe

The recipe applies to every Phase 3 applet. Per-applet entries diverge from it only on the items they explicitly call out.

### Files touched per port

- `vendor/O_C-Phazerville/software/src/applets/<Applet>.h` - read-only vendor source.
- `shim/include/applet_indices.h` - add one `kAppletXxx` enum value in alphabetical position before `kAppletCount`. Append-region in practice: every Phase 3 applet inserts into a different alpha slot, so the file changes are non-overlapping for parallel branches but the integration step rewrites the whole enum block. Treat as a sequenced gate.
- `shim/include/HemispheresFactory.h` - add four entries, all in alphabetical position:
  - `#include "<Applet>.h"` in the include block.
  - Entry in `applet_enum_strings()`' `names[]`.
  - Entry in `kMaxAppletSize` and `kMaxAppletAlign` `cmax(...)` chains.
  - Entry in `applet_factory()`' `table[]` (`&make_applet<Applet>`).
- `harness/tests/applet_test_helpers.h` - add `uint64_t pack_<applet>(...)` declaration in `namespace hem_test`, mirroring the layout described in the per-applet entry. Skip for applets with an empty `OnDataRequest()` (see Binary and Switch).
- `harness/tests/applet_test_helpers.cpp` - implement `pack_<applet>`. Skip for empty-`OnDataRequest` applets.
- `harness/tests/test_hemispheres.cpp` - add `using hem_shim::kAppletXxx`, optional per-applet `<applet>_set(hi, ...)` helper, and N `TEST_CASE`s under tag `[<applet>]`.
- `applets/Hemispheres.cpp` - no edit per applet.

### Pack helper signature

Pack helpers mirror the vendor `OnDataRequest` byte-by-byte. They live in `hem_test` and have this shape (Cumulus is the canonical example, see `harness/tests/applet_test_helpers.cpp:163`):

```cpp
uint64_t pack_<applet>(<typed_params>) {
    uint64_t data = 0;
    data |= ((uint64_t)(<field_with_bias>) & <mask>) << <offset>;
    // repeat per field in vendor OnDataRequest order
    // explicitly zero any gap bits left by vendor Pack calls
    return data;
}
```

Rules:

- Use `int` for each field at the helper boundary so tests pass signed values; apply the bias inside the helper.
- Use the same bias the vendor uses on `OnDataReceive`. If `OnDataReceive` does `field = Unpack(...) - 9`, the helper passes `((uint64_t)(field + 9) & mask) << offset`.
- For sub-byte fields, AND with the field-width mask, not `0xFF`. `(value & 0x07)` for a 3-bit field.
- If the vendor `OnDataRequest` skips any bit positions between `Pack` calls (gap bits), the helper must not write into those positions. Leave them zero by not OR-ing anything there. This matters for preset round-trip stability; see `Cumulus` bits 11..12.
- For applets whose `OnDataRequest()` returns `0` (no serialised state), do not declare a `pack_<applet>` helper. The round-trip test asserts `OnDataRequest() == 0` directly.

### Test setup primitive

Every test starts with `setup_applet(idx, side = LEFT)` (defined in `harness/tests/test_hemispheres.cpp:40`):

```cpp
auto s = setup_applet(kAppletXxx);
```

`s` is `{loaded, alg, bus, hi}`. The applet's `Start()` has run by the time `setup_applet` returns.

### Bus driving

- Single-sample gate edge (rising edge fires Clock(side) once, Gate(side) reads false next scan): `set_gate(bus, LEFT, channel, frame_offset, 8)`.
- Sustained gate (rising edge + Gate(side) reads true): `hold_gate(bus, LEFT, channel, 8)`.
- Flat CV across the buffer: `set_cv(bus, LEFT, channel, volts, 8)`.
- Always `clear_bus(bus)` between scenarios. Bus state persists across `step_n_frames` calls.
- The fourth argument `8` is `numFramesBy4` (32 frames per buffer = 8 quads).

### Stepping

`step_n_frames(loaded, alg, bus, n_samples)` issues `ceil(n_samples / 32)` `step()` calls. Tests reason in samples; the helper translates. One `step()` is one full 32-sample buffer.

Inside one `step()`, the shim runs the vendor Controller about `numFrames / 3 = 10` times (`ticks_this_step`). A single rising edge on a gate input triggers `Clock(side)` exactly once, but `clocked[ch]` stays asserted for all 10 inner Controller calls in that buffer (see Cumulus CU2 commentary at `harness/tests/test_hemispheres.cpp:1264`). Tests must account for this: an ADD-on-Clock applet adds 10 times per buffer, not once.

### Output reading

- Gate at a frame: `read_gate_at(bus, LEFT, channel, frame_offset, 8)` returns `bool`.
- CV at a frame: `read_cv_at(bus, LEFT, channel, frame_offset, 8)` returns `float` volts.
- Assertions on CV use `Approx(target).margin(...)` with margins sized to vendor int rounding (typically 0.01-0.1V).

### RNG

Applets that use `random_word()` or `random()` route through the shim's `hem_rng_state` xorshift32 global. Tests that need reproducible RNG output call `seed_hem_rng(0xDEADBEEF)` after `setup_applet`. Choose any constant; reproducibility within a test is what matters.

### TEST_CASE shape

Each applet ships at minimum these cases under tag `[<applet>]`:

- `Start defaults match vendor`: read `OnDataRequest()` right after `setup_applet`, decode each field with the inverse of the pack helper, assert against the values seen in `Start()`. For empty-`OnDataRequest` applets, assert `OnDataRequest() == 0`.
- `OnDataReceive then OnDataRequest round-trip preserves all serialised fields`: build a packed value with non-default field values inside the vendor's `constrain` ranges, call `OnDataReceive(packed)`, read `OnDataRequest()`, decode and compare. For empty-`OnDataRequest` applets, this case is the same as defaults.
- One case per major Controller branch (math op, CV-controlled mode, gate behavior, time-based path, etc.). Use the vendor source as the spec; one case per documented behavior is the target. Phase 2 averaged 6 cases per applet.

Field-value choices for the round-trip case must stay inside the vendor's `OnDataReceive` constraints. `OnDataReceive` clamps on receipt, so out-of-range packed values cannot be recovered (Cumulus CU5 demonstrates this for outmode clamp 0..7).

### Integration ordering

The integration step is sequenced. It edits `applet_indices.h` and `HemispheresFactory.h` in alphabetical order for all 17 applets at once, then runs the full host test suite (`make test-applets`) and verifies the `arm` build (`make arm`). Implementer commits land on their isolated branches; the integration commit picks them up via cherry-pick or merge, resolves the alpha-ordering inserts mechanically, and pushes a single integrated branch.

## Per-applet entries

Each entry lists: vendor file, category (always A for Phase 3), bit layout (verified against source at `file:line`), Start() defaults (with `file:line`), key observables, applet-specific test concerns, and the closest already-ported analogue.

### Binary

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Binary.h:29-115`.
- Category: A.
- `OnDataRequest` (`Binary.h:65-68`): returns 0. No serialised state.
- `Start()` (`Binary.h:35`): empty.
- Observables: `Out(0)` is a 4-bit binary sum scaled by `B0Val = HEMISPHERE_MAX_CV/15`; `Out(1)` is a count of high bits scaled by `CVal = HEMISPHERE_MAX_CV/4`. Bits: `bit[0]=Gate(0)`, `bit[1]=Gate(1)`, `bit[2]=In(0) > HEMISPHERE_3V_CV`, `bit[3]=In(1) > HEMISPHERE_3V_CV`. See `Binary.h:71-94`.
- Test concerns: empty `OnDataRequest`; no pack helper; assert `OnDataRequest() == 0` for round-trip. Three behavior cases: all-low yields zero on both outs; all-high yields full sum on Out(0) and 4*CVal on Out(1); CV thresholds compare against `HEMISPHERE_3V_CV` (3V), so 2V on In(0) leaves bit[2] low and 4V drives it high.
- Analogue: Compare (CV thresholding) with the no-pack pattern of GatedVCA.

### ClockDivider

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ClockDivider.h`.
- Category: A.
- `OnDataRequest` (`ClockDivider.h:91-98`): 32 bits used. Per channel `i` in {0,1}: bits `[0+i*8, 8) = div[i] + 32`, bits `[16+i*8, 8) = divmult[1+i*2].steps + 32`. Both fields are biased by +32 to encode negative multipliers.
- `Start()` (`ClockDivider.h`): `divmult[0].steps = 2; divmult[2].steps = 4`. (`div[]` is in-class initialized; check the field declaration in source.)
- Observables: Per-side clock division on Clock(0)/Clock(1) inputs; gate outputs on Out(0)/Out(1) at the divided rate.
- Test concerns: Two-channel pack helper, both fields biased by +32. Behavior cases: div=2 produces an output gate every second input clock; div=4 every fourth. Default-state case asserts the in-class field initialisers.
- Analogue: ClkToGate (per-side gate timing, two-field pack helper with bias).

### ClockSkip

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ClockSkip.h`.
- Category: A.
- `OnDataRequest` (`ClockSkip.h:68-73`): 14 bits. `p[0]` at `[0,7)`, `p[1]` at `[7,7)`. Both `p[i]` are skip-probability percentages 0..100.
- `Start()` (`ClockSkip.h:36-41`): `p[0] = 100, p[1] = 75`.
- Observables: Per-side probabilistic clock pass on Clock(ch) -> ClockOut(ch). Uses `random_word()` and so the shim RNG.
- Test concerns: Seed `hem_rng_state` per scenario. Behavior cases: p=100 always passes (gate out for every input clock); p=0 always skips (no gate out). Round-trip with p=37, 73.
- Analogue: Brancher (RNG-driven probabilistic gate routing, single 7-bit field).

### EnvFollow

- Vendor: `vendor/O_C-Phazerville/software/src/applets/EnvFollow.h`.
- Category: A.
- `OnDataRequest` (`EnvFollow.h:108-115`): 16 bits. `gain[0]` at `[0,5)`, `gain[1]` at `[5,5)`, `duck[0]` at `[10,1)`, `duck[1]` at `[11,1)`, `speed - 1` at `[12,4)` (biased -1 on pack, +1 on unpack).
- `Start()` (`EnvFollow.h:43-50`): `gain[0]=gain[1]=10`, `duck[0]=0`, `duck[1]=1`, `speed` defaulted in-class.
- Observables: Each channel reads `abs(In(ch))` peak over the last `HEM_ENV_FOLLOWER_SAMPLES=166` ticks and emits a scaled CV envelope on `Out(ch)`. `duck` inverts the envelope.
- Test concerns: Apply a steady CV on `set_cv(LEFT, 0, 4.0, 8)`, step ~200 samples to let the peak detector saturate, assert `Out(0)` tracks. The +1/-1 speed bias must round-trip through the pack helper.
- Analogue: Slew (CV-to-CV transformation with per-side params, time-based settling).

### PolyDiv

- Vendor: `vendor/O_C-Phazerville/software/src/applets/PolyDiv.h`.
- Category: A.
- `OnDataRequest` (`PolyDiv.h:140-149`): 20 bits used. `div_enabled` at `[0,8)` (8-bit bitmask). `divider[i].steps` at `[8+i*6, 6)` for `i in {0,1}` (the Hemisphere `ForAllChannels` macro iterates two channels in this applet).
- `Start()` (`PolyDiv.h`): empty body; `divider[]` and `div_enabled` use in-class initialisers.
- Observables: Polyrhythmic clock divider; per-channel `divider[i].steps` defines the integer ratio.
- Test concerns: Two channels, one 6-bit field each. Behavior cases: set steps=2 and steps=3, drive 12 Clock(0) edges, assert Out(0) fires 6 times and Out(1) fires 4 times. Round-trip with `div_enabled=0x03, divider[0].steps=2, divider[1].steps=3`.
- Analogue: ClkToGate (per-side divider, simple pack helper).

### ProbabilityDivider

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ProbabilityDivider.h`.
- Category: A.
- `OnDataRequest` (`ProbabilityDivider.h:179-189`): 40 bits. `weight_1` at `[0,4)`, `weight_2` at `[4,4)`, `weight_4` at `[8,4)`, `weight_8` at `[12,4)`, `loop_length` at `[16,8)`, `loop_linker.GetSeed()` at `[24,16)`.
- `Start()` (`ProbabilityDivider.h:60-71`): all weights 0, `loop_length=0`, loop counters zeroed.
- Observables: Probabilistic clock divider; each weight controls the relative likelihood that an input clock fires after 1, 2, 4, or 8 input pulses.
- Test concerns: Seed `hem_rng_state` for reproducibility. Behavior case: only `weight_1` non-zero produces an output gate on every Clock(0) edge; only `weight_8` non-zero produces a gate every 8 edges (over 80 input clocks, count outputs and assert 8..12). Round-trip preserves all four weights, loop_length, and seed.
- Analogue: Brancher (RNG-driven routing) with the multi-weight bit layout of Cumulus.

### ResetClock

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ResetClock.h`.
- Category: A.
- `OnDataRequest` (`ResetClock.h:109-115`): 17 bits. `length - 1` at `[0,5)` (biased -1 on pack, +1 on unpack; length is 1..32). `offset` at `[5,5)`. `spacing` at `[10,7)`.
- `Start()` (`ResetClock.h:32-40`): `length=8, offset=0, spacing=6, cursor=0, ticks_since_clock=0, pending_clocks=0, position=0`.
- Observables: Clock(0) increments `position`; when `position == offset` the applet fires a Reset (ClockOut(1)). `length` defines the loop modulus. `spacing` defines the gate-out duration in ticks.
- Test concerns: Length-1 bias on pack helper. Behavior case: length=4, offset=0; drive 4 Clock(0) edges and assert Reset fires on the first. Length=4, offset=2; assert Reset fires on the third edge.
- Analogue: ClkToGate (per-channel timing modifier with offset/length).

### RndWalk

- Vendor: `vendor/O_C-Phazerville/software/src/applets/RndWalk.h`.
- Category: A.
- `OnDataRequest` (`RndWalk.h:124-133`): 31 bits. `yClkSrc` at `[0,1)`, `yClkDiv` at `[1,4)`, `range` at `[5,8)`, `step` at `[13,8)`, `smoothness` at `[21,8)`, `cvRange` at `[29,2)`.
- `Start()` (`RndWalk.h:99-109`): per-channel `currentVal[ch]=0, currentOut[ch]=0`; `cursor=0`. Field defaults from in-class initialisers.
- Observables: Random-walk CV output on Clock(0). `range` caps amplitude; `step` is the per-tick walk size; `smoothness` adds slew.
- Test concerns: Seed RNG. Behavior case: smoothness=0, step=1; drive 100 Clock(0) edges and assert `Out(0)` stays within `+/- range * (HEMISPHERE_MAX_CV / 255)`. Round-trip preserves all six fields.
- Analogue: Slew (per-side CV with time-domain smoothing) and Brancher (RNG-driven).

### RunglBook

- Vendor: `vendor/O_C-Phazerville/software/src/applets/RunglBook.h`.
- Category: A.
- `OnDataRequest` (`RunglBook.h:71-75`): 16 bits. `threshold` at `[0,16)`. No bias.
- `Start()` (`RunglBook.h:33`): `threshold = ONE_OCTAVE * 2 = 3072` (= 2V).
- Observables: 8-bit Rungler shift register on Clock(0). `Out(0)` is the register's high byte; `Out(1)` is a single bit.
- Test concerns: Seed RNG. Default-state case asserts threshold=3072. Round-trip with threshold=5000. Behavior cases: drive Clock(0) 8 times with In(0) above and below threshold, assert `Out(0)` walks across a range of values; freeze via Gate(1) and assert register rotates rather than shifts.
- Test budget: at the Phase 3 ceiling (~50 step() calls total). Each Clock(0) edge needs a set_gate buffer plus a clear_bus buffer, so 8 input clocks = 16 step() calls; three behavior cases plus default plus round-trip lands near the budget. Be sparing if adding cases.
- Analogue: Slew (single CV-out with one signed time-domain param).

### Schmitt

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Schmitt.h`.
- Category: A.
- `OnDataRequest` (`Schmitt.h:70-75`): 32 bits. `low` at `[0,16)`, `high` at `[16,16)`. Both in vendor int CV units (`0..HEMISPHERE_MAX_CV = 9216`). No bias.
- `Start()` (`Schmitt.h:30-35`): `low=3200` (~2.08V), `high=3968` (~2.58V).
- Observables: Per-channel schmitt trigger. `Out(ch)` goes high when `In(ch)` crosses above `high` and stays high until `In(ch)` falls below `low`.
- Test concerns: Behavior case: set `In(0) = 1V` (below low), step, assert `Out(0)` low; set `In(0) = 3V` (above high), step, assert `Out(0)` high; drop to `2.4V` (between low and high), assert `Out(0)` stays high (hysteresis); drop to `1V`, assert `Out(0)` low. Round-trip preserves both fields.
- Analogue: Compare (CV threshold to gate).

### ShiftGate

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ShiftGate.h`.
- Category: A.
- `OnDataRequest` (`ShiftGate.h:63-71`): 32 bits with a 6-bit gap. `length[0] - 1` at `[0,4)` (biased), `length[1] - 1` at `[4,4)` (biased), `trigger[0]` at `[8,1)`, `trigger[1]` at `[9,1)`, **gap at `[10,6)` must be zero**, `reg[0]` at `[16,16)`.
- `Start()` (`ShiftGate.h:32-38`): `length[ch] = 4`, `trigger[ch] = ch`, `reg[ch] = random(0, 0xffff)`. The Start-time `random()` call uses Phazerville's seeded RNG; pack-helper round-trip is the only safe way to set a deterministic `reg[0]` for tests.
- Observables: 16-step shift register on Clock(0); `Out(ch)` reflects `reg[ch] & 1`; on each Clock the register shifts.
- Test concerns: Gap-bit guard in pack helper (mirror Cumulus). Default-state case asserts only the non-random fields (length, trigger). Round-trip with `length=8, trigger=1/0, reg[0]=0x5555` exercises both gates of the register over 16 Clock(0) edges.
- Analogue: Cumulus (gap-bit-guarded pack helper) and ClkToGate (per-side trigger configuration).

### Stairs

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Stairs.h`.
- Category: A.
- `OnDataRequest` (`Stairs.h:236-242`): 8 bits. `steps` at `[0,5)`, `dir` at `[5,2)`, `rand` at `[7,1)`.
- `Start()` (`Stairs.h:78-100`): `steps=1, dir=0, rand=0`. (`steps=1` because the field stores `steps - 1` semantics in some implementations; verify against source.)
- Observables: Clock(0) steps a CV up, down, or up-down through `n` divisions of `HEMISPHERE_MAX_CV`. `dir` selects direction. `rand=1` enables random stepping.
- Test concerns: 8-bit pack helper. Behavior case: `steps=4, dir=0 (up)`; drive 4 Clock(0) edges; assert `Out(0)` takes 4 distinct ascending values that span 0 to `HEMISPHERE_MAX_CV * 3/4`. Round-trip with `steps=8, dir=2, rand=1`.
- 10x clocked-multiplier caveat: one rising edge on Gate(0) makes the shim's `clocked[ch]` flag stay asserted for all `ticks_this_step = numFrames / 3 = 10` inner Controller calls in that buffer. A single set_gate pulse therefore steps `curr_step` 10 times, not once. Adjust step counts in assertions accordingly. Precedent and detailed mechanic in Cumulus CU2 (`harness/tests/test_hemispheres.cpp:1264`); do not re-explain it in the implementer commit.
- Analogue: Cumulus (stepped CV on clock with multi-field pack).

### Switch

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Switch.h`.
- Category: A.
- `OnDataRequest` (`Switch.h:64-67`): returns 0. No serialised state. `active[0], active[1]` are runtime-only.
- `Start()` (`Switch.h:32-35`): `active[0]=1, active[1]=1`.
- Observables: Per-side passthrough of `In(active[ch])` to `Out(ch)`. Clock(ch) toggles `active[ch]` between 0 and 1.
- Test concerns: Empty `OnDataRequest`; no pack helper; round-trip asserts `OnDataRequest() == 0`. Behavior cases: with active=1, set `set_cv(LEFT, 0, 1.0, 8)` and `set_cv(LEFT, 1, 4.0, 8)`; assert `Out(0)` reads 4V. After Clock(0) toggles active[0] to 0, assert `Out(0)` reads 1V.
- Analogue: GatedVCA / Button (empty-`OnDataRequest` pattern).

### Trending

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Trending.h`.
- Category: A.
- `OnDataRequest` (`Trending.h:109-115`): 16 bits. `assign[0]` at `[0,4)`, `assign[1]` at `[4,4)`, `sensitivity` at `[8,8)`.
- `Start()` (`Trending.h:34-41`): `assign[0]=0, assign[1]=1, sensitivity=40, sample_countdown=0, result[ch]=0, fire[ch]=0`.
- Observables: Per-side slope detector. `Out(ch)` fires a brief gate when `In(assign[ch])` rises faster than `sensitivity`.
- Test concerns: Behavior case: assign[0]=0, sensitivity=40; ramp `set_cv(LEFT, 0, v, 8)` from 0 to 5V across 50 buffers; assert `Out(0)` fires at least once. Drop CV and assert it stops. Round-trip preserves three fields.
- Analogue: Compare (CV-to-gate threshold).

### Voltage

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Voltage.h`.
- Category: A.
- `OnDataRequest` (`Voltage.h:78-85`): 21 bits with a 1-bit gap. `voltage[0] + 256` at `[0,9)` (biased; semitone units, range `+/- VOLTAGE_MAX = 72`), **gap at `[9,1)` must be zero**, `voltage[1] + 256` at `[10,9)`, `gate[0]` at `[19,1)`, `gate[1]` at `[20,1)`. `VOLTAGE_INCREMENTS=128` hem units per semitone, so `voltage*128` is the emitted hem-int.
- `Start()` (`Voltage.h:31-37`): `voltage[0]=VOLTAGE_MAX=72` (=6V), `voltage[1]=VOLTAGE_MIN=-72` (=-6V), `gate[ch]=0`.
- Observables: `Out(ch)` emits `voltage[ch] * VOLTAGE_INCREMENTS` continuously, or only when `Gate(ch)` is high if `gate[ch]=1`.
- Test concerns: Gap-bit guard like Cumulus. Default-state case asserts +6V / -6V on Out(0)/Out(1). Round-trip with `voltage=24/-12, gate=1/0`.
- Analogue: AttenuateOffset (per-side biased-offset pack helper with gap bits).

## Spot check

Three claims in the recipe section were spot-checked against the existing code:

1. **"`step_n_frames` issues ceil(n/32) `step()` calls."** Verified at `harness/tests/applet_test_helpers.cpp:63-70`: `const int framesPerStep = 32; int steps = (n_samples + framesPerStep - 1) / framesPerStep;` then a `for` loop of `loaded->factory->step(...)` calls.
2. **"Assertions on CV use `Approx(target).margin(...)` with margins sized to vendor int rounding."** Verified at `harness/tests/test_hemispheres.cpp:161`: `REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.01f));` (Calculate C2). Other examples use `0.05f` and `0.1f` margins for larger signals.
3. **"A single rising edge on a gate input triggers Clock(side) exactly once, but clocked[ch] stays asserted for all 10 inner Controller calls in that buffer."** Verified at `harness/tests/test_hemispheres.cpp:1264-1280` (Cumulus CU2) and the surrounding comment block, which derives the 10-tick count from `ticks_this_step = numFrames / 3 = 32 / 3`.

These three claims are load-bearing for the recipe's accuracy. If any one of them drifts later, the recipe must be updated and the spot-check rerun.
