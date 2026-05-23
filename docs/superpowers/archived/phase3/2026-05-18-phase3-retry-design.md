# Design: Phase 3 Hemisphere applet ports (retry)

Date: 2026-05-18
Status: Draft (autonomous retry of 2026-05-18 spec)
Owner: Dan
Brainstorm: `docs/superpowers/brainstorms/2026-05-18-phase3-retry-brainstorm.md`
Vendor pin: `vendor/O_C-Phazerville` at `7800d929`

## Context

Phase 1 and Phase 2 ported 14 Hemisphere applets (13 vendor plus the shim-local `Empty`) into the disting NT compatibility shim. Each has a host-side Catch2 test suite that drives the plug-in through the NT API. Phase 3 attempt 1 dispatched 15 category-A applet ports in parallel. The retrospective at `docs/superpowers/abort-reports/2026-05-18-phase3-attempt-1-retrospective.md` identifies four root causes: per-entry spec defects, parent-agent worktree dispatch from `main`, prose-only implementer contract, and a missing recipe rule for the shim's 10x clocked multiplier. This retry addresses each.

Retry scope: 11 category-A applets. Four demoted to Phase 4 (Binary, ResetClock, ShiftGate, Trending). Pair-applet variants remain deferred.

## Recipe

The recipe applies to every in-scope retry applet. Per-applet entries below state only the deltas from this recipe.

### Files touched per port

Integration task owns:

- `shim/include/applet_indices.h` - one `kApplet<Name>` enum value per applet, alpha-ordered, before `kAppletCount`.
- `shim/include/HemispheresFactory.h` - five entries per applet, all alpha-positioned: `#include "<Applet>.h"`; entry in `applet_enum_strings()` `names[]`; entry in `kMaxAppletSize` `cmax(...)` chain; entry in `kMaxAppletAlign` `cmax(...)` chain; entry in `applet_factory()` `table[]` at the index matching the enum order.
- `shim/include/PhzIcons.h` - one `extern const uint8_t <icon>[8];` stub per applet.
- Any vendor macro shim files needed (none for the in-scope retry set; ShiftGate's `HEMISPHERE_3V_CV` need is what demoted it).

Implementer task owns:

- `vendor/O_C-Phazerville/software/src/applets/<Applet>.h` - read-only vendor source.
- `harness/tests/applet_test_helpers.h` - one `uint64_t pack_<applet>(...)` declaration in `namespace hem_test`. Skip when the applet's `OnDataRequest()` returns 0 (Switch).
- `harness/tests/applet_test_helpers.cpp` - one `pack_<applet>` implementation. Skip when no declaration.
- `harness/tests/test_hemispheres.cpp` - `using hem_shim::kApplet<Name>;`, optional per-applet `<applet>_set(hi, ...)` helper, N `TEST_CASE`s under tag `[<applet>]`.

`applets/Hemispheres.cpp` is unchanged per applet. The factory table is in `shim/include/HemispheresFactory.h`, not in `Hemispheres.cpp`.

The implementer never commits to `shim/include/applet_indices.h`, `shim/include/HemispheresFactory.h`, `shim/include/PhzIcons.h`, or any other shim infrastructure file. The pre-commit hook installed in each implementer worktree by the parent agent enforces this and rejects commits that stage forbidden-surface changes.

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

- Single-sample gate edge: `set_gate(bus, LEFT, channel, frame_offset, 8)`. Rising edge fires `Clock(side)` once; `Gate(side)` reads false on the next scan.
- Sustained gate: `hold_gate(bus, LEFT, channel, 8)`. Rising edge plus `Gate(side)` reads true.
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

Applets that call `random_word()` or `random()` route through the shim's `hem_rng_state` xorshift32 global. Tests seed reproducibly via `seed_hem_rng(0xDEADBEEF)` after `setup_applet`.

### TEST_CASE shape

Each applet ships at minimum these cases under tag `[<applet>]`:

- `Start defaults match vendor`: read `OnDataRequest()` immediately after `setup_applet`, decode each field, assert against `Start()` values. Empty-`OnDataRequest` applets assert `OnDataRequest() == 0`.
- `OnDataReceive then OnDataRequest round-trip preserves all serialised fields`: pack a non-default value inside the vendor's `constrain` ranges, call `OnDataReceive(packed)`, read `OnDataRequest()`, decode and compare. Empty-`OnDataRequest` applets repeat the defaults case.
- One case per major Controller branch. Phase 2 averaged 6 cases per applet.

Field values for the round-trip case must stay inside the vendor's `OnDataReceive` constraints. `OnDataReceive` clamps on receipt, so out-of-range packed values cannot be recovered.

### Bus-level behavioral assertions and the 10x clocked multiplier

The shim runs the vendor `Controller()` 10 times per `step()` call (`ticks_this_step = numFrames / 3 = 10`). A single rising edge on `Clock(side)` sets `clocked[side] = true` once, but `clocked[side]` stays asserted for all 10 inner Controller calls in that buffer. See `harness/tests/test_hemispheres.cpp:1264-1280` (Cumulus CU2) for the canonical commentary and assertion math.

Consequence: for any applet whose `Controller()` advances an internal counter, accumulator, or toggle inside `if (Clock(ch))`, one input edge produces 10 internal advances per buffer, not one.

Two valid coverage shapes apply:

1. Model the multiplier explicitly. Compute the expected post-buffer state assuming 10 inner Controller iterations. Cumulus CU2, Stairs ST2-ST3, and RunglBook RB2 are the precedent. Pure-state assertions on `Out(ch)` after a known number of input edges remain reliable provided the assertion math accounts for the 10x.
2. Drop bus-level fire-count assertions; restrict coverage to round-trip plus state-injection. The applet's serialised state is verified by round-trip; the per-branch behavior is verified by injecting state via `OnDataReceive` and reading `OnDataRequest`. ProbabilityDivider is the precedent.

Per-applet entries that involve internal counters consuming `Clock(ch)` must state which shape they use.

### Integration ordering

The integration step is sequenced after all implementer commits land. It edits `applet_indices.h`, `HemispheresFactory.h`, and `PhzIcons.h` in alphabetical order across the entire retry set in a single commit. It then runs `make test-applets` and `make arm`. Implementer commits land on their isolated branches; the integration commit picks them up via cherry-pick or merge, resolves the alpha-ordered inserts mechanically, and pushes a single integrated branch.

## Per-applet entries

Each in-scope entry lists: vendor file with line range, category, Status, cherry-pick disposition, `OnDataRequest` bit layout with `file:line`, `Start()` defaults with `file:line`, `Controller()` observables with `file:line`, test concerns including 10x acknowledgment if applicable, shim prereqs, closest already-ported analogue.

### ClockDivider

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ClockDivider.h:23-157`.
- Category: A.
- Status: In scope for retry.
- Cherry-pick: re-run (archive `clock_divider` predates 10x rule).
- `OnDataRequest` (`ClockDivider.h:91-98`): 32 bits. Per channel `i` in {0,1}: `div[i] + 32` at `[0+i*8, 8)`; `divmult[1+i*2].steps + 32` at `[16+i*8, 8)`. Both fields biased +32.
- `Start()` (`ClockDivider.h:42-45` plus in-class init at `ClockDivider.h:122` and `util/clkdivmult.h:7`): `div[0]=1, div[1]=2`; `divmult[0].steps=2, divmult[2].steps=4`; `divmult[1].steps=1, divmult[3].steps=1` (default). Packed default: `33 | 34<<8 | 33<<16 | 33<<24`.
- `Controller()` (`ClockDivider.h:53-71`): Per inner tick, `clocked = Clock(0)`. Each enabled `divmult[ch*2].Tick(clocked)` runs and feeds `divmult[ch*2+1].Tick(trig)`. `ClockOut(ch)` fires when both stages return true. `Clock(1)` triggers `Reset()`.
- Test concerns: 10x affected. `divmult[0].Tick(true)` runs 10 times per buffer per input edge; `clock_count` advances 10 per buffer. Tests model the 10x explicitly (Cumulus CU2 precedent): with `div[0]=2`, driving 1 Clock(0) buffer fires `ClockOut(0)` 5 times across the inner ticks. Cases: defaults match in-class initialisers; round-trip with `div[0]=4, div[1]=-2, divmult[1].steps=3, divmult[3].steps=2`; one case per branch covering positive division and negative multiplication using state-injection only (no bus fire-count claim).
- Shim prereqs: icon stub only.
- Analogue: ClkToGate (per-side timing, two-field pack helper with bias).

### ClockSkip

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ClockSkip.h`.
- Category: A.
- Status: In scope for retry.
- Cherry-pick: re-run.
- `OnDataRequest` (`ClockSkip.h:68-73`): 14 bits. `p[0]` at `[0,7)`, `p[1]` at `[7,7)`. Both are skip percentages 0..100.
- `Start()` (`ClockSkip.h:36-41`): `p[0]=100, p[1]=75`.
- `Controller()`: For each channel, on `Clock(ch)`, call `random_word()` and skip the output gate if the result mod 100 exceeds `p[ch]`. `ClockOut(ch)` otherwise.
- Test concerns: No 10x risk (probability eval stateless per edge). Seed RNG before each scenario. Cases: defaults; round-trip with `p[0]=37, p[1]=73`; behavior: `p=100` always passes (gate per edge), `p=0` always skips.
- Shim prereqs: icon stub only.
- Analogue: Brancher (RNG-driven probabilistic routing, single 7-bit field).

### EnvFollow

- Vendor: `vendor/O_C-Phazerville/software/src/applets/EnvFollow.h`.
- Category: A.
- Status: In scope for retry.
- Cherry-pick: re-run.
- `OnDataRequest` (`EnvFollow.h:108-115`): 16 bits. `gain[0]` at `[0,5)`, `gain[1]` at `[5,5)`, `duck[0]` at `[10,1)`, `duck[1]` at `[11,1)`, `speed - 1` at `[12,4)` (biased -1 on pack, +1 on unpack).
- `Start()` (`EnvFollow.h:43-50`): `gain[0]=gain[1]=10, duck[0]=0, duck[1]=1, speed` defaulted in-class, `max[ch]=0, countdown=166`.
- `Controller()`: Each tick samples `abs(In(ch))`, tracks peak over `HEM_ENV_FOLLOWER_SAMPLES=166` ticks, emits scaled envelope on `Out(ch)`. `duck[ch]=1` inverts the envelope (signal ducks under gate-driven dynamics).
- Test concerns: No 10x fire-count risk (continuous CV reading, not gated counters). Cases: defaults; round-trip with `gain=15/8, duck=1/0, speed=4`; behavior: apply `set_cv(LEFT, 0, 4.0, 8)`, step ~200 samples for peak detector to settle, assert `Out(0)` tracks. Speed bias must round-trip.
- Shim prereqs: icon stub only.
- Analogue: Slew (CV-to-CV transformation with per-side params).

### PolyDiv

- Vendor: `vendor/O_C-Phazerville/software/src/applets/PolyDiv.h`.
- Category: A.
- Status: In scope for retry.
- Cherry-pick: re-run (archive `poly_div` dirty plus spec defect).
- `OnDataRequest` (`PolyDiv.h:140-149`): 32 bits. `div_enabled` at `[0,8)` (8-bit bitmask covering all four channels). `divider[i].steps` at `[8+i*6, 6)` for `i in {0,1,2,3}` (vendor `ForAllChannels` iterates four channels in this applet, not two).
- `Start()` (`PolyDiv.h`): empty body; `divider[]` and `div_enabled` use in-class initialisers.
- `Controller()`: Polyrhythmic clock divider; per enabled channel `i`, `divider[i].steps` defines the integer ratio. `Clock(0)` drives `divider[i].Poke()` each inner tick; on the appropriate count, `TrigOut(ch)` fires for the channel assigned to that output.
- Test concerns: 10x affected. Per inner tick with `clocked[0]=true`, `divider[i].Poke(true)` runs 10 times per buffer. Tests model the 10x explicitly. Cases: defaults match in-class initialisers; round-trip with `div_enabled=0x0F, divider[0..3].steps={2,3,4,5}`; behavior cases use state-injection plus 10x assertion math (do not claim "fires once per N edges").
- Shim prereqs: icon stub only.
- Analogue: ClkToGate (per-side divider, multi-channel pack helper).

### ProbabilityDivider

- Vendor: `vendor/O_C-Phazerville/software/src/applets/ProbabilityDivider.h`.
- Category: A.
- Status: In scope for retry.
- Cherry-pick: re-run.
- `OnDataRequest` (`ProbabilityDivider.h:179-189`): 40 bits. `weight_1` at `[0,4)`, `weight_2` at `[4,4)`, `weight_4` at `[8,4)`, `weight_8` at `[12,4)`, `loop_length` at `[16,8)`, `loop_linker.GetSeed()` at `[24,16)`.
- `Start()` (`ProbabilityDivider.h:40-52`): all weights 0, `loop_length=0`, `loop_index=0`, `loop_step=0`, `skip_steps=0`, `GateOut(ch, false)`.
- `Controller()`: On `Clock(0)`, weighted random pick across `weight_{1,2,4,8}` selects how many input edges to skip; `skip_steps` countdown drives `ClockOut(0)` or `ClockOut(1)` on fire.
- Test concerns: 10x affected. `skip_steps` decrements per inner Controller tick under `clocked[0]=true`; bus-level "1 fire per N edges" is unreachable. Coverage shape: round-trip plus state-injection only. Cases: defaults; round-trip with `weight_1=15, weight_2=10, weight_4=5, weight_8=0, loop_length=8, seed=0xCAFE`; state-injection verifies the four weights are honored by reading post-`OnDataReceive` `OnDataRequest()`. No bus-level fire-count assertion.
- Shim prereqs: icon stub only.
- Analogue: Brancher (RNG-driven) with the multi-weight bit layout of Cumulus.

### RndWalk

- Vendor: `vendor/O_C-Phazerville/software/src/applets/RndWalk.h`.
- Category: A.
- Status: In scope for retry.
- Cherry-pick: re-run.
- `OnDataRequest` (`RndWalk.h:124-133`): 31 bits. `yClkSrc` at `[0,1)`, `yClkDiv` at `[1,4)`, `range` at `[5,8)`, `step` at `[13,8)`, `smoothness` at `[21,8)`, `cvRange` at `[29,2)`.
- `Start()` (`RndWalk.h:44-54` plus in-class init at `RndWalk.h:162-167`): `currentVal[ch]=0, currentOut[ch]=0, cursor=0`; in-class defaults `yClkSrc=0, yClkDiv=1, range=20, step=20, smoothness=20, cvRange=3`.
- `Controller()` (`RndWalk.h:56-85`): On `Clock(0)`, X-channel walk advances by `random(1, stepLimit) * RECIP_MAX_STEP * max_val * 0.5f` in direction biased by `PROB_UP/PROB_DN`. Y-channel gated by `Clock(yClkSrc)` and `clkMod % yClkDiv`. Output is smoothed via `alpha` first-order filter.
- Test concerns: 10x present on walk advancement but amplitude-bounded by `range`, so bus-level "stays within +/- range * (HEMISPHERE_MAX_CV / 255)" assertion remains reliable. Seed RNG. Cases: defaults; round-trip preserves all six fields; behavior: smoothness=0, step=1, 100 Clock(0) edges, assert `Out(0)` bounded.
- Shim prereqs: icon stub only.
- Analogue: Slew + Brancher (per-side CV with smoothing, RNG-driven).

### RunglBook

- Vendor: `vendor/O_C-Phazerville/software/src/applets/RunglBook.h`.
- Category: A.
- Status: In scope for retry.
- Cherry-pick: re-run (archive `rungl_book` dirty).
- `OnDataRequest` (`RunglBook.h:71-75`): 16 bits. `threshold` at `[0,16)`. No bias.
- `Start()` (`RunglBook.h:33`): `threshold = ONE_OCTAVE * 2 = 3072` (= 2V).
- `Controller()`: 8-bit Rungler shift register driven by `Clock(0)`. `Out(0)` is the register's high byte; `Out(1)` is a single bit. `Gate(1)` freezes the register (rotate instead of shift).
- Test concerns: 10x present on register shifts but folded into expected state. Per buffer with `clocked[0]=true`, register shifts 10 times. Tests assert post-buffer register state using 10x math (RB2 precedent). Seed RNG. Cases: defaults assert threshold; round-trip with threshold=5000; behavior: drive Clock(0) with `In(0)` above and below threshold, assert `Out(0)` traverses expected register values after 10-shift step; Gate(1) freeze case asserts rotation rather than shift.
- Shim prereqs: icon stub only.
- Analogue: Slew (single CV-out with one signed time-domain param).

### Schmitt

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Schmitt.h`.
- Category: A.
- Status: In scope for retry.
- Cherry-pick: re-run.
- `OnDataRequest` (`Schmitt.h:70-75`): 32 bits. `low` at `[0,16)`, `high` at `[16,16)`. Both in vendor int CV units (0..`HEMISPHERE_MAX_CV = 9216`). No bias.
- `Start()` (`Schmitt.h:30-35`): `low=3200` (~2.08V), `high=3968` (~2.58V), `cursor=0`, `gate_countdown=0`.
- `Controller()`: Per-channel schmitt trigger. `Out(ch)` goes high when `In(ch)` crosses above `high` and stays high until `In(ch)` falls below `low`.
- Test concerns: No 10x risk (hysteresis comparator, no accumulator). Cases: defaults; round-trip with `low=2000, high=4000`; behavior: set `In(0)=1V`, assert `Out(0)` low; set `In(0)=3V`, assert high; drop to `2.4V`, assert hysteresis (stays high); drop to `1V`, assert low.
- Shim prereqs: icon stub only.
- Analogue: Compare (CV threshold to gate).

### Stairs

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Stairs.h`.
- Category: A.
- Status: In scope for retry.
- Cherry-pick: re-run.
- `OnDataRequest` (`Stairs.h:236-242`): 8 bits. `steps` at `[0,5)`, `dir` at `[5,2)`, `rand` at `[7,1)`.
- `Start()` (`Stairs.h:46-59` plus field declarations): `steps=1, dir=0, rand=0, cursor=0, curr_step=0, step_cv_lock=false, position_cv_lock=false, reset_gate=false, reverse=false, cv_out=0, cv_rand=0`.
- `Controller()` (`Stairs.h:104-202`): On `Clock(0)`, advance `curr_step` per direction (up/down/up-down/random). `Out(0)` emits `cv_out` scaled to `HEMISPHERE_MAX_CV * curr_step / steps`. `ClockOut(1)` fires on reset/wrap at lines 104, 119, 144, 153.
- Test concerns: 10x present on `curr_step` advances; tests model the multiplier explicitly (Cumulus CU2 precedent). Cases: defaults; round-trip with `steps=8, dir=2, rand=1`; behavior with `steps=4, dir=0 (up)`: 1 Clock(0) buffer advances `curr_step` 10 times (= 4-step wrap 2x + 2 advances), assert final `Out(0)` matches expected post-wrap CV. State-injection covers `rand=1`.
- Shim prereqs: icon stub only.
- Analogue: Cumulus (stepped CV on clock with multi-field pack).

### Switch

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Switch.h:23-104`.
- Category: A.
- Status: In scope for retry.
- Cherry-pick: re-run (active[] semantics correction needed).
- `OnDataRequest` (`Switch.h:64-67`): returns 0. No serialised state. `active[]` is runtime-only.
- `Start()` (`Switch.h:30-31`): `active[0]=1, active[1]=1`. Vendor uses `active[ch] = step + 1` where `step in {0,1}`, so the runtime value set is `{1,2}`, not `{0,1}`.
- `Controller()` (`Switch.h:36-49`): `Clock(0)` toggles `active[0]` between 1 and 2. `Out(0) = In(active[0] - 1)` (i.e., `In(0)` or `In(1)`). `Out(1)` selects between `In(0)` and `In(1)` based on `Gate(1)`.
- Test concerns: 10x risk on `Clock(0)` toggle. Each input edge buffer toggles `active[0]` 10 times, ending in the same state. The attempt-1 SW2 case (drive Clock(0) once, assert toggle visible) is defeated; tests must either drive an odd number of Clock(0) edges in distinct buffers (each buffer toggles 10 times = even = no net change, so each input pulse must span a separate `step()` call) or use `Gate(1)` on the channel-1 switch (not affected by the 10x). Empty `OnDataRequest`; no pack helper; round-trip asserts `OnDataRequest() == 0`. Cases: defaults assert `active[]={1,1}`; behavior on channel 1 uses `Gate(1)` to select between `In(0)` and `In(1)`.
- Shim prereqs: icon stub only.
- Analogue: GatedVCA / Button (empty-`OnDataRequest` pattern).

### Voltage

- Vendor: `vendor/O_C-Phazerville/software/src/applets/Voltage.h:22-134`.
- Category: A.
- Status: In scope for retry.
- Cherry-pick: re-run (archive `voltage` dirty).
- `OnDataRequest` (`Voltage.h:78-85`): 21 bits with a 1-bit gap. `voltage[0] + 256` at `[0,9)` (biased; semitone units, range `+/- VOLTAGE_MAX = 72`). Gap at `[9,1)` must be zero. `voltage[1] + 256` at `[10,9)`. `gate[0]` at `[19,1)`. `gate[1]` at `[20,1)`. `VOLTAGE_INCREMENTS=128` hem units per semitone.
- `Start()` (`Voltage.h:34-39`): `voltage[0]=VOLTAGE_MAX=72` (=6V), `voltage[1]=VOLTAGE_MIN=-72` (=-6V), `gate[ch]=0`.
- `Controller()` (`Voltage.h:41-55`): If `gate[ch]=1` (normally-off), `Out(ch) = voltage[ch] * 128` only when `Gate(ch)` high. If `gate[ch]=0` (normally-on), `Out(ch) = voltage[ch] * 128` except when `Gate(ch)` high.
- Test concerns: No 10x risk (per-tick `Gate(ch)` polled, no internal counter). Gap-bit guard at position 9 mirrors Cumulus. Cases: defaults assert +6V on Out(0), -6V on Out(1); round-trip with `voltage[0]=24, voltage[1]=-12, gate[0]=1, gate[1]=0`; behavior: normally-on inverts on gate, normally-off enables on gate.
- Shim prereqs: icon stub only.
- Analogue: AttenuateOffset (per-side biased-offset pack helper with gap bit).

### Deferred entries

The following four applets are deferred from this retry. Each carries a one-line block stating the deferral reason and the prereq that would unblock it.

- **Binary** (`Binary.h`): Deferred pending shim work. Needs `SegmentDisplay` shim port, `HEMISPHERE_3V_CV` macro at shim layer, `binaryCounter` icon. Three independent prereqs; full Phase 4 effort.
- **ResetClock** (`ResetClock.h`): Deferred pending spec rewrite. Per `docs/superpowers/abort-reports/2026-05-18-resetclock-spec-mismatch.md`, the attempt-1 spec contradicts vendor `Controller()` semantics on `pending_clocks`, `offset_mod`, and `position`. Full rewrite required; re-anchor on Burst analogue with `RC_TICKS_PER_MS=175` and re-evaluate category (likely C time-sensitive).
- **ShiftGate** (`ShiftGate.h`): Deferred pending shim work. Needs `HEMISPHERE_3V_CV` macro at shim layer (vendor `ShiftGate.h:29` references it). One-line shim addition but out of retry scope.
- **Trending** (`Trending.h`): Deferred pending shim work. Vendor `Trending.h:81` calls `HemisphereApplet::Changed(int ch)`, which does not exist in `shim/include/HemisphereApplet.h`. Shim port required.

## Spec footer

### Recipe spot-check

Three load-bearing claims in the Recipe section were verified against the existing code:

1. **"`step_n_frames` issues ceil(n/32) `step()` calls."** Verified at `harness/tests/applet_test_helpers.cpp:63-70`: `const int framesPerStep = 32; int steps = (n_samples + framesPerStep - 1) / framesPerStep;` then a for-loop of `loaded->factory->step(...)` calls.
2. **"Assertions on CV use `Approx(target).margin(...)` with margins sized to vendor int rounding."** Verified at `harness/tests/test_hemispheres.cpp:161`: `REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.01f));` Other examples use 0.05f and 0.1f for larger signals.
3. **"One `step()` runs the vendor `Controller()` exactly 10 times; `clocked[ch]` stays asserted for all 10 inner Controller calls in that buffer."** Verified at `harness/tests/test_hemispheres.cpp:1264-1280` (Cumulus CU2) and the surrounding commentary block, which derives the 10-tick count from `ticks_this_step = numFrames / 3 = 32 / 3 = 10`.

### Per-entry verification

Three randomly-selected per-applet entries were traced end-to-end against the entry's `Start()` and `Controller()` claims.

1. **ClockDivider**. `OnDataRequest` layout (`ClockDivider.h:91-98`) matches the entry: `div[i] + 32` at `[0+i*8, 8)`, `divmult[1+i*2].steps + 32` at `[16+i*8, 8)`. `Start()` (`ClockDivider.h:42-45`) sets `divmult[0].steps=2, divmult[2].steps=4`; in-class `div[2] = {1, 2}` at line 122; `ClkDivMult.steps = 1` default at `util/clkdivmult.h:7`. `Controller()` (`ClockDivider.h:53-71`) confirms `Tick(clocked)` chain and `ClockOut(ch)` semantics. 10x risk real: `clocked = Clock(0)` reads `clocked[0]` per inner tick; `Tick(true)` runs 10x per buffer per input edge; `clock_count` advances 10x. Entry's "model 10x explicitly" coverage shape is correct. **Confirmed.**

2. **RndWalk**. `OnDataRequest` layout (`RndWalk.h:124-133`) matches the entry: six fields at the claimed bit positions. `Start()` (`RndWalk.h:44-54` plus in-class init at `RndWalk.h:162-167`) confirms `currentVal[ch]=0, currentOut[ch]=0` and field defaults. `Controller()` (`RndWalk.h:56-85`) confirms X-channel walk on `Clock(0)`, Y-channel gated by `Clock(yClkSrc)` and `clkMod % yClkDiv`, exponential smoothing via `alpha`. 10x risk present on walk advances per buffer; amplitude bounded by `range`, so the entry's bounded-amplitude test approach remains reliable. **Confirmed.**

3. **Voltage**. `OnDataRequest` layout (`Voltage.h:78-85`) matches the entry: `voltage[0]+256` at `[0,9)`, gap at `[9,1)`, `voltage[1]+256` at `[10,9)`, `gate[0]` at `[19,1)`, `gate[1]` at `[20,1)`. `Start()` (`Voltage.h:34-39`) confirms `voltage[0]=VOLTAGE_MAX=72, voltage[1]=VOLTAGE_MIN=-72, gate[ch]=0`. `Controller()` (`Voltage.h:41-55`) confirms the gate-polarity branching: `gate[ch]=1` is normally-off (output only when Gate(ch) high), `gate[ch]=0` is normally-on (output except when Gate(ch) high). No 10x risk (stateless gate poll). **Confirmed.**

Result: 0 of 3 entries contradict vendor reality. Below the "more than 1 of 3" gate; no expanded audit triggered.
