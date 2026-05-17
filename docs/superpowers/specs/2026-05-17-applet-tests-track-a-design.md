# Design: Applet Logic Tests (Track A, Phase 1)

Date: 2026-05-17
Status: Approved (brainstorm)
Owner: Dan
Worktree: `worktree-applet-tests`

## Context

Plan G shipped 8 new Tier 2 applets to the Hemispheres compatibility shim plug-in. They were hardware-verified by visual smoke on the disting NT. There is no automated coverage of their logic, so every shim helper change requires another hardware retest to catch regressions. Hardware retests are slow (deploy + manual UI walk + audio probe) and inconsistent across sessions.

This spec replaces hardware-loop logic checks with host-side automated tests. Track A means: link the Hemispheres plug-in source directly into a Catch2 test binary, drive the plug-in's standard NT API (parameters, bus frames, custom UI), and assert on output bus values plus serialised state. The bus is the contract; if outputs match expected values for given inputs, the applet logic is correct.

Hardware retests remain authoritative for display rendering, audio-rate CPU budget, and real-preset integration. Track A explicitly does not cover those.

This spec is Phase 1 only: pilot on one Tier 1 applet (Calculate) and one Tier 2 applet (Brancher). Phase 2 (remaining 11 applets) is a separate plan after Phase 1 reviews well.

## Decisions

- Scope: pilot on Calculate (Tier 1, CV math) and Brancher (Tier 2, gate routing + RNG). One per tier, picked to exercise the broadest mechanic surface (CV in / CV out math with mode select, gate edge detection with seeded RNG).
- Depth: spec-driven from vendor source. Each TEST_CASE derives from a documented behavior line in the vendor applet header. ~22 cases for Phase 1.
- Test seam: cast `_NT_algorithm*` to `HemispheresInstance*` (fields already public), reach `instance->left` / `instance->right`, call `OnDataReceive(packed_state)` to inject internal applet state in one call. No customUi loop. No vendor changes. No port to NT-native params.
- Layout: single combined Catch2 binary `build/host/test_hemispheres`. Cases grouped by `[applet-name]` tags.
- Makefile: new `make test-applets`, chained into the existing `make test` after YAML scenarios.
- RNG: shim's existing `hem_rng_state` xorshift32 global is written to a fixed seed before each scenario. Mix of exact-value assertions (single roll) and statistical bounds (1000 rolls, ratio).
- CV unit convention: assertions in vendor int units (HEMISPHERE_MIN_CV..HEMISPHERE_MAX_CV). Translation helpers `volts_to_int(float)` / `int_to_volts(int)` live in a test helper module.
- Time stepping: helper `step_n_frames(alg, bus, n_samples)` wraps the 32-frame-per-step API. Tests reason in sample counts. 32-sample resolution is the limit; sub-32 timing tests deferred to Phase 2 if needed.
- Display assertions: skipped in Phase 1. Smoke case calls `draw()` once to assert no crash.
- Mutation testing: deferred to Track B or later.

## Architecture

### Build pipeline

Hemispheres compiles host-side cleanly with `NT_HEM_HOST_SIM=1` + shim include paths. Verified by probe: `clang++ ... -c -o build/host/Hemispheres.host.o applets/Hemispheres.cpp` succeeds with zero warnings and exports `pluginEntry`. The shipped 73 KB host object resolves the weak `pluginEntry` fallback in `plugin_loader.cpp` automatically; no runtime ELF loader needed.

New Makefile targets:

```
build/host/Hemispheres.host.o: applets/Hemispheres.cpp $(SHIM_DEPS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -c -o $@ $<

build/host/test_hemispheres: harness/tests/test_hemispheres.cpp \
                              harness/tests/applet_test_helpers.cpp \
                              build/host/Hemispheres.host.o \
                              $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

.PHONY: test-applets
test-applets: build/host/test_hemispheres
	./build/host/test_hemispheres
```

`make test` gets `test-applets` appended to its prerequisite list.

### Per-test wiring

```
1. nt::reset_runtime()                     // zero buses + screen
2. hem_rng_state = 0xDEADBEEF              // deterministic RNG per test
3. auto loaded = nt::load_plugin()         // resolves Hemispheres factory
4. _NT_algorithm* alg = loaded->algorithm
5. alg->v[kHemSelLeft]  = kAppletBrancher  // pick applet
6. loaded->factory->parameterChanged(alg, kHemSelLeft)
7. auto* hi = reinterpret_cast<HemispheresInstance*>(alg);
8. hi->left->OnDataReceive(pack_brancher(p=75, choice=0))   // inject state
```

`HemispheresInstance::left` / `right` are already public members (see `shim/include/hemispheres_shim.h`). The cast is safe within the test harness because `HemispheresInstance` derives from `_NT_algorithm`.

### Bus convention

Hemispheres' routing params (`kHemGateInA..D`, `kHemCvInA..D`, `kHemCvOutA..D`) point at NT bus indices 1..16 (bus 0 is the unmapped sentinel per `tests/reference/cv_scaling.txt`). Channels A and B are the left side's two applet channels; channels C and D are the right side's. Phase 1 tests use default routing:

- Left side, channel 0 (A): gate bus 1, CV in bus 5, output bus 13
- Left side, channel 1 (B): gate bus 2, CV in bus 6, output bus 14
- Right side, channel 0 (C): gate bus 3, CV in bus 7, output bus 15
- Right side, channel 1 (D): gate bus 4, CV in bus 8, output bus 16

Inside the applet, `Gate(0)` / `In(0)` / `Out(0)` refers to that side's first channel; `Gate(1)` / `In(1)` / `Out(1)` refers to its second. The shim binds those to the bus indices above. Tests write input bus values to `bus[N * numFrames + frame_offset]`, call `step`, then read output bus values from the same buffer (NT bus frames are in/out).

### Test helper module

`harness/tests/applet_test_helpers.{h,cpp}` provides a side, channel, and axis-addressed API. Tests never touch raw bus indices.

Core types:

```cpp
enum HemSide  { LEFT = 0, RIGHT = 1 };
enum HemAxis  { GATE_IN, CV_IN, OUT };
```

Bus access. Tests never need to know the `kHemGateInA..D` / `kHemCvInA..D` / `kHemCvOutA..D` layout.

- `int bus_index(HemSide side, int channel, HemAxis axis)`: returns 1..16. Example: `(LEFT, 0, GATE_IN)` returns 1.
- `void set_gate(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4)`: single-sample pulse on that side and channel's gate input bus.
- `void set_cv(float* bus, HemSide side, int channel, float volts, int numFramesBy4)`: flat CV across all frames on that side and channel's CV input bus.
- `bool read_gate_at(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4)`: sample the output bus at a specific frame, return true if above gate threshold.
- `float read_cv_at(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4)`: sample the output bus at a specific frame.

Applet lifecycle.

- `void select_applet(loaded, alg, HemSide side, AppletIndex idx)`: one-liner replacement for the `v[]=...; parameterChanged(...)` pair.
- `HemisphereApplet* get_applet(HemispheresInstance*, HemSide side)`: typed accessor; returns `hi->left` or `hi->right`.
- `HemispheresInstance* as_instance(_NT_algorithm*)`: typed cast.

Stepping.

- `int step_n_frames(loaded, alg, float* bus, int n_samples)`: issues ceil(n_samples/32) `step` calls, returns actual frames advanced.

Unit conversion.

- `int volts_to_int(float v)`: float volts to vendor int (linear, anchored on `HEMISPHERE_MAX_CV` = 7680 = 5V per vendor convention).
- `float int_to_volts(int v)`: inverse.

RNG.

- `void seed_hem_rng(uint32_t seed)`: writes the shim's `hem_rng_state` global. Tests call this once at the top of any case that touches RNG. Naming the helper makes the discipline visible at the call site.

Per-applet state injection.

- `uint64_t pack_brancher(int p)`: mirrors vendor `OnDataRequest`: `PackLocation{0,7}` for p in 0..100.
- `uint64_t pack_calculate(int op_left, int op_right)`: mirrors vendor: `PackLocation{0,8}` and `{8,8}`.

Phase 2 adds one `pack_<applet>(...)` per applet that has a non-trivial `OnDataRequest`. See "Per-applet OnDataRequest bit layouts" below.

Two Phase 2 applets serialise no state (Button, GatedVCA). Their internal state is UI-driven only. For those, tests inject state by calling the applet's UI-event methods directly through the test seam, e.g. `get_applet(hi, LEFT)->OnEncoderMove(+5)` or `OnButtonPress()`. No `pack_<applet>(...)` exists for those two.

Brancher's `OnDataRequest` packs only `p`, not `choice`. For tests that need to inject a specific `choice` mid-run (B7 OnButtonPress), the helper calls `get_applet(hi, LEFT)->OnButtonPress()` to flip `choice`.

### Adding a new applet (Phase 2 onboarding)

Three steps.

1. Read the vendor header at `vendor/O_C-Phazerville/software/src/applets/<Applet>.h`. Extract the `OnDataRequest` bit layout from the `Pack(data, PackLocation{offset, width}, value)` calls. Note any bias values added before packing (e.g. AttenuateOffset adds 256 to make signed offsets positive in the unsigned packed field). Cross-reference the "Per-applet OnDataRequest bit layouts" table below.
2. Add `uint64_t pack_<applet>(args...)` to `applet_test_helpers.{h,cpp}` mirroring the vendor `Pack` calls. If the applet serialises no state (Button, GatedVCA), skip this step.
3. Add `TEST_CASE("<applet> <behavior>", "[<applet>]")` blocks to `test_hemispheres.cpp`. Use only the side, channel, axis helpers; do not hardcode bus indices. Seed RNG with `seed_hem_rng(...)` at the top of any case that touches RNG. Assert in vendor int units via `volts_to_int(...)` where comparing CVs.

For the applet's invariants, read vendor `Controller()` line by line. Each branch yields a TEST_CASE. Pin every constant the vendor exposes (clamps, defaults, bias values) as an explicit assertion so re-vendoring drift is caught.

### Per-applet OnDataRequest bit layouts

Extracted verbatim from each vendor header. Use as the source of truth when writing `pack_<applet>(...)`.

| Applet | Bits used | Layout |
|---|---|---|
| AttenuateOffset | 36 | offset[0] (0..8, value + 256), offset[1] (10..18, value + 256), level[0] (19..26, value + ATTENOFF_MAX_LEVEL*2), level[1] (27..34, same bias), mix (35) |
| Brancher | 7 | p (0..6, range 0..100) |
| Burst | 40 | number (0..7), spacing (8..15), div (16..23, value + 8), jitter (24..31), accel (32..39) |
| Button | 0 | no state serialised; transient UI state only |
| Calculate | 16 | operation[0] (0..7), operation[1] (8..15) |
| ClkToGate | 64 | per side i in 0..1: width[i] (i*32+0..i*32+6), abs(range[i]) (i*32+8..i*32+14), range[i] sign bit (i*32+15), skip[i] (i*32+16..i*32+22) |
| Compare | 8 | level (0..7) |
| Cumulus | 17 | accoperator (0..2), b_constant (3..6), outmode[0] (7..10), gap bits 11..12 unused, outmode[1] (13..16) |
| GateDelay | 22 | time[0] (0..10), time[1] (11..21) |
| GatedVCA | 0 | no state serialised; transient UI state only |
| Logic | 16 | operation[0] (0..7), operation[1] (8..15) |
| Slew | 16 | rise (0..7), fall (8..15) |
| TLNeuron | 21 | dendrite_weight[0] (0..4, value + 9), dw[1] (5..9, +9), dw[2] (10..14, +9), threshold (15..20, value + 27) |

Notes.

- Bias values exist because vendor packs into unsigned fields but underlying members are signed. The `pack_<applet>(...)` helper applies the bias internally so test callers pass natural signed values.
- ClkToGate fills 64 bits exactly. Two sides times 32 bits each, no room for additional state.
- Cumulus has a 2-bit gap at positions 11..12. The vendor `OnDataReceive` skips those bits. Future re-vendoring may fill them; `pack_cumulus(...)` should explicitly zero those bits to avoid stale state leaking.
- Button and GatedVCA serialise nothing. Preset save/load resets their state. Tests must inject state via UI-event methods (`OnEncoderMove`, `OnButtonPress`) within the same TEST_CASE.

### Determinism

- `nt::reset_runtime()` zeros all bus and screen state per test (existing in harness).
- `hem_rng_state` writes to a fixed seed (`0xDEADBEEF` or per-case seed).
- No threads, no time-dependent code paths in the shim.
- Catch2 runs single-threaded by default.

## Implementation

### Files created

- `harness/tests/test_hemispheres.cpp`: Catch2 TEST_CASE entries for Brancher + Calculate.
- `harness/tests/applet_test_helpers.h`: declarations.
- `harness/tests/applet_test_helpers.cpp`: implementations.

### Files modified

- `Makefile`: adds `build/host/Hemispheres.host.o`, `build/host/test_hemispheres`, `.PHONY: test-applets`, chains `test-applets` into `test`.

### No changes to

- `applets/Hemispheres.cpp`
- `shim/` headers or sources
- `vendor/` (submodules pinned)
- `harness/src/plugin_loader.{cpp,h}` (existing weak-symbol pattern is sufficient)

### TDD order

1. Build pipeline + smoke case S1. Confirms link, parameterChanged, draw all work.
2. Helper module skeleton: `step_n_frames`, `volts_to_int`, `set_bus_constant`, `pulse_gate`.
3. Calculate C2-C8 (deterministic combinatorial math). One case at a time; each red, then green.
4. Calculate C9 (S&H pre-clock), C10 (S&H post-clock).
5. Calculate C11 (Rnd unipolar range), C12 (Rnd clocked latch).
6. Calculate C13 (serialise round-trip via OnDataRequest).
7. Brancher B1 (Start defaults), B2 (p=100), B3 (p=0).
8. Brancher B5 (logical clock pulse), B6 (flip-flop).
9. Brancher B7 (OnButtonPress flip), B8 (encoder clamp), B9 (serialise round-trip).
10. Brancher B4 (statistical fairness, 1000 rolls). Last because it depends on all prior gate/clock infrastructure.

Each case lands as its own commit (`feat: test brancher B2 p=100 deterministic`).

## Test scenarios

### Brancher (tag `[brancher]`, 9 cases)

| # | Behavior | Setup | Assertion |
|---|---|---|---|
| B1 | Start defaults | construct, select Brancher | `OnDataRequest` returns 50 in bits [0,7] (Start sets `p = 50`; `choice = 0` is not serialised so not directly asserted here, but covered indirectly by B2's deterministic-output assertion) |
| B2 | Deterministic p=100 | inject p=100, physical Clock(0) gate, one step | bus[out_left_0] high, bus[out_left_1] low |
| B3 | Deterministic p=0 | inject p=0, physical Clock(0) gate, one step | bus[out_left_1] high, bus[out_left_0] low |
| B4 | Statistical fairness | inject p=50, seed RNG, 1000 Clock(0) edges | ratio of out_0 vs out_1 in `[0.46, 0.54]` |
| B5 | Logical clock pulse | inject p=100, Clock(0) without physical gate | out_left_0 receives single-tick ClockOut pulse, out_left_1 low |
| B6 | Flip-flop mode | inject p=100, Clock(1) twice with no Clock(0) | first Clock(1) → out_0 high; second Clock(1) → out_1 high, out_0 low |
| B7 | OnButtonPress flip | construct with choice=0, `hi->left->OnButtonPress()`, physical gate on Clock(0) | gate appears on out_1, not out_0 |
| B8 | p encoder clamp | call `OnEncoderMove(+5)` 30 times from p=50 | `OnDataRequest` returns p clamped to 100 |
| B9 | Serialise round-trip | call `OnDataReceive(pack(p=73))`, then `OnDataRequest` | returned packed value unpacks to p=73 |

### Calculate (tag `[calculate]`, 12 cases)

| # | Behavior | Setup | Assertion |
|---|---|---|---|
| C1 | Start defaults | construct, select Calculate | `OnDataRequest` packs (0, 1) for (operation[0], operation[1]); `rand_clocked` defaults are covered indirectly by C12's clocked-latch assertion |
| C2 | MIN | inject op=MIN both ch, In(0)=v(2), In(1)=v(4) | Out(0) = Out(1) = v(2) |
| C3 | MAX | inject op=MAX both ch, same inputs | Out = v(4) |
| C4 | SUM clamp high | inject op=SUM, In(0)=HEMISPHERE_MAX_CV, In(1)=v(0.5) | Out clamped to HEMISPHERE_MAX_CV |
| C5 | SUM clamp low | inject op=SUM, In(0)=HEMISPHERE_MIN_CV, In(1)=v(-0.5) | Out clamped to HEMISPHERE_MIN_CV |
| C6 | DIFF absolute | inject op=DIFF, In(0)=v(1), In(1)=v(3) | Out = v(2); swap inputs → Out = v(2) |
| C7 | MEAN | inject op=MEAN, In(0)=v(1), In(1)=v(3) | Out = v(2) |
| C8 | Asymmetric quirk | inject op[0]=MIN, op[1]=MAX, In(0)=v(1), In(1)=v(3) | Out(0) = v(1), Out(1) = v(3) (both channels read In(0) and In(1)) |
| C9 | S&H pre-clock | inject op=S&H both ch, In(0)=v(2), no Clock | Out stays at 0 |
| C10 | S&H post-clock | inject op=S&H ch 0, Clock(0), step ADC-lag-many frames | Out(0) = current In(0) |
| C11 | Rnd unipolar range | inject op=Rnd+, seed RNG, 100 unclocked steps | Out(0) in `[0, HEMISPHERE_MAX_CV)` for all samples |
| C12 | Rnd clocked latch | inject op=Rnd+, Clock(0) once, 10 unclocked steps | Out(0) constant across unclocked steps |
| C13 | Serialise round-trip | inject op[0]=5, op[1]=7 via OnDataReceive, then OnDataRequest | unpacks to (5, 7) |

### Smoke (tag `[smoke]`, 1 case)

| # | Behavior | Setup | Assertion |
|---|---|---|---|
| S1 | Loads, steps, draws | select Brancher left, Calculate right, 10 steps, call draw() | no crash; all output bus values finite |

Total: 22 cases.

## Verification

### Local

```
make clean
git submodule update --init --recursive
make arm                    # baseline: ARM build still clean
make test                   # YAML scenarios + applet tests pass
make test-applets           # explicit applet test run
./build/host/test_hemispheres                # all 22 cases
./build/host/test_hemispheres "[brancher]"   # 9 cases
./build/host/test_hemispheres "[calculate]"  # 12 cases
./build/host/test_hemispheres "[smoke]"      # 1 case
```

Expected:

```
===============================================================================
All tests passed (22 assertions in 22 test cases)
```

### Regression coverage

After Phase 1 lands these regressions get caught automatically:

- Shim helper edits that break Brancher gate routing or Calculate math
- Vendor re-vendoring that drifts applet behavior
- Hemispheres factory wiring breakage in the applet-swap path
- Serialise/deserialise format changes
- RNG implementation changes (statistical case diverges)

### Out of scope

- Tier 1 remainder: AttenuateOffset, Burst, Logic, Slew
- Tier 2 remainder: Button, ClkToGate, Compare, Cumulus, GateDelay, GatedVCA, TLNeuron
- Display rendering correctness (gfx framebuffer)
- Audio-rate CPU budget
- Real preset integration with neighboring algorithms
- Mutation testing
- Per-applet binary split

### Hardware re-verification gate

After Phase 1 merges, one hardware deploy via `make deploy-sysex` to confirm Hemispheres.o still loads and Brancher / Calculate behave the same. If hardware passes, Phase 1 is complete and Phase 2 brainstorms separately.
