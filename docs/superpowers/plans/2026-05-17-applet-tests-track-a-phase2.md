# Applet Logic Tests (Track A, Phase 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend Track A host-side automated logic tests to the remaining 11 applets in the Hemispheres compatibility shim: AttenuateOffset, Burst, Logic, Slew (Tier 1 remainder); Button, ClkToGate, Compare, Cumulus, GateDelay, GatedVCA, TLNeuron (Tier 2 remainder).

**Architecture:** Reuse Phase 1 infrastructure verbatim (Catch2 binary, side/channel/axis helper API, `HemispheresInstance` test seam, seeded xorshift32 RNG). Add one preparatory refactor that unifies per-applet setup helpers into a single generic `setup_applet(AppletIndex, HemSide)`. Each new applet gets its own `pack_<applet>(...)` helper plus a TEST_CASE block tagged `[<applet>]`. Bit layouts are re-verified against the current vendor headers before each `pack_` helper is written.

**Tech Stack:** C++14 (host), Catch2, GNU Make, clang++.

**Spec reference:** `docs/superpowers/specs/2026-05-17-applet-tests-track-a-design.md` (Phase 1 design doc; Phase 2 prep notes section is the contract for this plan).

---

## File Structure

Modified files:

- `harness/tests/applet_test_helpers.h`: add new `pack_<applet>(...)` declarations for each Phase 2 applet (10 new helpers; Button and GatedVCA serialise nothing).
- `harness/tests/applet_test_helpers.cpp`: add the matching definitions.
- `harness/tests/test_hemispheres.cpp`: add per-applet `TEST_CASE` blocks (each applet gets its own tag, e.g., `[logic]`, `[atten_off]`, etc.). Refactor existing `setup_calculate_left()` / `setup_brancher_left()` into a single generic `setup_applet(AppletIndex, HemSide)`.

No new files. No changes to `applets/Hemispheres.cpp` (the Phase 1 test seam `get_applet_impl(int)` already covers everything Phase 2 needs). No changes to shim sources. No changes to the Makefile.

---

## Conventions used in tasks

Every task takes the worktree from a green state to a green state. After each task: `make arm` still builds, `make test` still passes.

Commit message format: `feat(test-applets): <applet> <case range>` for new tests, `refactor(test-applets): <what>` for refactors. Always pass `-c commit.gpgsign=false` to git commit per project convention.

Constants resolved during Phase 1, used unchanged here:

- Gate "high" threshold in bus floats: `> 0.5f` (shim's `read_gate`).
- Gate "high" output value from `GateOut(ch, true)`: `6.0V` on the bus.
- `ONE_OCTAVE = 1536`. `volts_to_int(v) = (int)std::lroundf(v * 1536.0f)`.
- `HEMISPHERE_MAX_CV = 9216 = 6V`, `HEMISPHERE_MIN_CV = -9216 = -6V`.
- `HEMISPHERE_MAX_INPUT_CV = 9216` (same as MAX_CV; the `Proportion(...)` callers use it for scaling).
- Default routing: left side (A, B) at gate bus 1/2, CV bus 5/6, output bus 13/14; right side (C, D) at gate bus 3/4, CV bus 7/8, output bus 15/16.
- Hemispheres has no `parameterChanged` callback. `select_applet` writes `alg->v[]`; the next `step()` performs the swap.
- `Controller()` runs `numFrames / 3` times per `step()` call (10 ticks per 32-frame step).

Helper inventory (from Phase 1):

- `bus_index(side, channel, axis)`, `set_gate`, `hold_gate`, `set_cv`, `read_gate_at`, `read_cv_at`, `clear_bus`
- `select_applet`, `get_applet`, `as_instance`, `step_n_frames`
- `volts_to_int`, `int_to_volts`, `seed_hem_rng`
- `pack_brancher`, `pack_calculate`

Phase 2 adds: `pack_atten_off`, `pack_burst`, `pack_logic`, `pack_slew`, `pack_clk_to_gate`, `pack_compare`, `pack_cumulus`, `pack_gate_delay`, `pack_tlneuron`. No pack helpers for Button or GatedVCA (vendor `OnDataRequest` returns 0; transient UI state only).

Test-seam pattern for state injection: cast `alg` to `HemispheresInstance*` via `as_instance`, call `get_applet(hi, side)->OnDataReceive(pack_<applet>(...))` to set internal state. For Button and GatedVCA: use `get_applet(hi, side)->OnEncoderMove(...)` or `OnButtonPress()` directly to set state, since they serialise nothing.

---

## Task 1: Generic setup_applet helper refactor

**Files:**
- Modify: `harness/tests/test_hemispheres.cpp`

The Phase 1 anon-namespace block has two near-identical setup helpers:

```cpp
struct CalcSetup   { nt::LoadedPlugin* loaded; _NT_algorithm* alg; float* bus; hem_shim::HemispheresInstance* hi; };
struct BranchSetup { nt::LoadedPlugin* loaded; _NT_algorithm* alg; float* bus; hem_shim::HemispheresInstance* hi; };

CalcSetup setup_calculate_left() { ... }
BranchSetup setup_brancher_left() { ... }
```

Phase 2 has 11 more applets. Repeating the per-applet pattern would yield 22 helpers (left + right per applet). Replace with a single generic helper.

- [ ] **Step 1: Refactor the anon-namespace block**

Replace the two structs and two functions with a unified version:

```cpp
struct AppletSetup {
    nt::LoadedPlugin* loaded;
    _NT_algorithm*    alg;
    float*            bus;
    hem_shim::HemispheresInstance* hi;
};

AppletSetup setup_applet(hem_shim::AppletIndex idx, HemSide side = LEFT) {
    nt::reset_runtime();
    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    auto* alg = loaded->algorithm;
    REQUIRE(alg != nullptr);
    select_applet(alg, side, idx);
    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    clear_bus(bus);
    step_n_frames(loaded, alg, bus, 32);  // triggers swap + Start
    return { loaded, alg, bus, as_instance(alg) };
}
```

Keep `calculate_set_op` (it is Calculate-specific) but the `CalcSetup` and `BranchSetup` structs become aliases (or are dropped entirely if all call sites can switch to `AppletSetup`).

Actually drop both old structs entirely. Replace `CalcSetup` and `BranchSetup` typedefs with the new `AppletSetup` everywhere they appear.

- [ ] **Step 2: Migrate all Phase 1 call sites**

For every existing TEST_CASE that calls `setup_calculate_left()` or `setup_brancher_left()`:

- Replace the function call with the generic form.
- Change the return-type aliases if needed (e.g., `auto s = setup_applet(kAppletCalculate);` works because `s.loaded`, `s.alg`, `s.bus`, `s.hi` are unchanged).

Use a search-and-replace:

- Replace `setup_calculate_left()` with `setup_applet(kAppletCalculate)`.
- Replace `setup_brancher_left()` with `setup_applet(kAppletBrancher)`.

`calculate_set_op` and other applet-specific helpers stay.

- [ ] **Step 3: Run full suite**

Run:

```bash
make test-applets
```

Expected: 23 cases / 827 assertions (same as Phase 1 end state).

Run:

```bash
make test
```

Expected: full suite clean.

- [ ] **Step 4: Commit**

```bash
git add harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "refactor(test-applets): unify per-applet setup into setup_applet(idx, side)"
```

---

## Task 2: Logic (Tier 1)

**Files:**
- Modify: `harness/tests/applet_test_helpers.h` (add `pack_logic` decl)
- Modify: `harness/tests/applet_test_helpers.cpp` (add `pack_logic` impl)
- Modify: `harness/tests/test_hemispheres.cpp` (add TEST_CASEs)

Vendor source: `vendor/O_C-Phazerville/software/src/applets/Logic.h`.

Logic has two gate inputs (`Gate(0)`, `Gate(1)`) and 7 operations: AND, OR, XOR, NAND, NOR, XNOR, CV-controlled. Each channel runs its op independently. CV-controlled mode (operation = 6) reads `In(ch)`, scales to [0, 5], selects which of the 6 underlying ops to apply per step.

Bit layout per spec table: `Pack(data, PackLocation {0,8}, operation[0]); Pack(data, PackLocation {8,8}, operation[1]);`. 16 bits used. Re-verify before writing the helper.

Default state from `Start()`: `operation[0] = 0` (AND), `operation[1] = 2` (XOR).

- [ ] **Step 1: Re-verify bit layout**

Run:

```bash
awk '/uint64_t OnDataRequest|OnDataReceive\(uint64_t/,/^[ \t]*}/' vendor/O_C-Phazerville/software/src/applets/Logic.h
```

Expected output: shows `Pack(data, PackLocation {0, 8}, operation[0])` and `Pack(data, PackLocation {8, 8}, operation[1])`. If the layout has changed, update the spec table and adjust pack/test code accordingly.

- [ ] **Step 2: Add `pack_logic` declaration**

In `harness/tests/applet_test_helpers.h`, after the last `pack_*` declaration, add:

```cpp
// Mirrors Logic::OnDataRequest packing: bits [0,8] = op_left, [8,8] = op_right.
// Ops: 0=AND, 1=OR, 2=XOR, 3=NAND, 4=NOR, 5=XNOR, 6=CV-controlled.
uint64_t pack_logic(int op_left, int op_right);
```

- [ ] **Step 3: Add implementation**

In `harness/tests/applet_test_helpers.cpp`:

```cpp
uint64_t pack_logic(int op_left, int op_right) {
    return ((uint64_t)(op_left  & 0xFF))
         | ((uint64_t)(op_right & 0xFF) << 8);
}
```

- [ ] **Step 4: Add a per-applet op-setter helper**

In the anon namespace of `harness/tests/test_hemispheres.cpp`:

```cpp
void logic_set_op(hem_shim::HemispheresInstance* hi, int op_left, int op_right) {
    get_applet(hi, LEFT)->OnDataReceive(pack_logic(op_left, op_right));
}
```

- [ ] **Step 5: Add TEST_CASEs**

Append after the existing Brancher cases:

```cpp
TEST_CASE("logic L1: Start defaults are AND, XOR", "[logic]") {
    auto s = setup_applet(kAppletLogic);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF)        == 0);  // AND
    REQUIRE(((packed >> 8) & 0xFF) == 2);  // XOR
}

TEST_CASE("logic L2: AND outputs s1 AND s2", "[logic]") {
    auto s = setup_applet(kAppletLogic);
    logic_set_op(s.hi, 0, 0);  // AND both

    // s1=low, s2=low -> 0
    clear_bus(s.bus);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);

    // s1=high, s2=low -> 0
    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);

    // s1=low, s2=high -> 0
    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 1, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);

    // s1=high, s2=high -> 1
    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    hold_gate(s.bus, LEFT, 1, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
}

TEST_CASE("logic L3: OR outputs s1 OR s2", "[logic]") {
    auto s = setup_applet(kAppletLogic);
    logic_set_op(s.hi, 1, 1);

    clear_bus(s.bus);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 1, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    hold_gate(s.bus, LEFT, 1, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
}

TEST_CASE("logic L4: XOR outputs s1 XOR s2", "[logic]") {
    auto s = setup_applet(kAppletLogic);
    logic_set_op(s.hi, 2, 2);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);  // s1=high only
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    hold_gate(s.bus, LEFT, 1, 8);  // both high -> XOR=0
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
}

TEST_CASE("logic L5: NAND, NOR, XNOR invert AND/OR/XOR", "[logic]") {
    auto s = setup_applet(kAppletLogic);

    // NAND s1=s2=high -> 0 (inverted AND)
    logic_set_op(s.hi, 3, 3);
    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    hold_gate(s.bus, LEFT, 1, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);

    // NOR s1=s2=low -> 1 (inverted OR)
    logic_set_op(s.hi, 4, 4);
    clear_bus(s.bus);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);

    // XNOR s1=s2=high -> 1 (inverted XOR)
    logic_set_op(s.hi, 5, 5);
    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    hold_gate(s.bus, LEFT, 1, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
}

TEST_CASE("logic L6: CV-controlled mode picks op from CV", "[logic]") {
    // Vendor Logic.h:62-67: when operation[ch] == 6, the actual op is selected
    // from CV by scaling abs(In(ch)) to [0, 5] and using that as the op index.
    auto s = setup_applet(kAppletLogic);
    logic_set_op(s.hi, 6, 6);  // CV-controlled both channels

    // CV near 0 -> idx 0 -> AND. s1=s2=high -> 1.
    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    hold_gate(s.bus, LEFT, 1, 8);
    // No CV written; In(0) == 0 -> idx 0 -> AND.
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
}

TEST_CASE("logic L7: serialise round-trip preserves ops", "[logic]") {
    auto s = setup_applet(kAppletLogic);
    get_applet(s.hi, LEFT)->OnDataReceive(pack_logic(5, 3));
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF)        == 5);
    REQUIRE(((packed >> 8) & 0xFF) == 3);
}
```

- [ ] **Step 6: Run tests**

```bash
./build/host/test_hemispheres "[logic]"
```

Expected: 7 cases pass.

```bash
make test
```

Expected: full suite clean.

- [ ] **Step 7: Commit**

```bash
git add harness/tests/applet_test_helpers.h harness/tests/applet_test_helpers.cpp harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): logic L1-L7 boolean ops + CV-controlled mode"
```

---

## Task 3: AttenuateOffset (Tier 1)

**Files:**
- Modify: `harness/tests/applet_test_helpers.h` (add `pack_atten_off` decl)
- Modify: `harness/tests/applet_test_helpers.cpp` (add `pack_atten_off` impl)
- Modify: `harness/tests/test_hemispheres.cpp` (add TEST_CASEs)

Vendor source: `vendor/O_C-Phazerville/software/src/applets/AttenuateOffset.h`.

AttenuateOffset is the first Phase 2 applet to use Modulate. It applies per-channel attenuation (`level[ch]` is a percent in [-200%, +200%]) and a per-channel offset (`offset[ch]` in semitones; `offset * ATTENOFF_INCREMENTS` adds to the int signal). Gate(1) high enables mix mode: Out(1) becomes Out(0) + Out(1). `Start()` sets `level[ch] = ATTENOFF_MAX_LEVEL` (= 100, full attenuation) and offset[ch] = 0.

Vendor `ATTENOFF_INCREMENTS` and `ATTENOFF_MAX_LEVEL` constants: look them up in `HSUtils.h` or `AttenuateOffset.h` before writing the test scaling.

Bit layout (spec table, 36 bits):
- `Pack(data, {0,9}, offset[0] + 256)`: 9 bits, biased.
- `Pack(data, {10,9}, offset[1] + 256)`: bit 9 unused (gap).
- `Pack(data, {19,8}, level[0] + ATTENOFF_MAX_LEVEL*2)`: biased.
- `Pack(data, {27,8}, level[1] + same)`: biased.
- `Pack(data, {35,1}, mix)`

- [ ] **Step 1: Re-verify constants**

Run:

```bash
grep -n 'ATTENOFF_INCREMENTS\|ATTENOFF_MAX_LEVEL' vendor/O_C-Phazerville/software/src/applets/AttenuateOffset.h vendor/O_C-Phazerville/software/src/HSUtils.h shim/include/HSUtils.h
```

Expected: numeric values for both constants. Note them down for the pack helper and the test margin choices.

- [ ] **Step 2: Re-verify bit layout**

Run:

```bash
awk '/uint64_t OnDataRequest|OnDataReceive\(uint64_t/,/^[ \t]*}/' vendor/O_C-Phazerville/software/src/applets/AttenuateOffset.h
```

Confirm the spec table row matches.

- [ ] **Step 3: Add `pack_atten_off` declaration**

In `harness/tests/applet_test_helpers.h`:

```cpp
// Mirrors AttenuateOffset::OnDataRequest packing.
// Biased fields (offset has +256, level has +ATTENOFF_MAX_LEVEL*2). Caller
// passes natural signed values; helper applies the bias.
// Layout: offset[0] (0,9) | offset[1] (10,9) | level[0] (19,8) | level[1] (27,8) | mix (35,1).
// Note: bit 9 is unused (gap between offset[0] and offset[1]).
uint64_t pack_atten_off(int offset_left, int offset_right,
                         int level_left, int level_right,
                         bool mix);
```

- [ ] **Step 4: Add implementation**

In `harness/tests/applet_test_helpers.cpp` (replace the constant values if `ATTENOFF_MAX_LEVEL` differs):

```cpp
namespace {
constexpr int kAttenOffMaxLevel = 100;  // mirrors ATTENOFF_MAX_LEVEL in vendor HSUtils.h
}

uint64_t pack_atten_off(int offset_left, int offset_right,
                         int level_left, int level_right,
                         bool mix) {
    uint64_t data = 0;
    data |= ((uint64_t)((offset_left  + 256) & 0x1FF));
    data |= ((uint64_t)((offset_right + 256) & 0x1FF)) << 10;
    data |= ((uint64_t)((level_left  + kAttenOffMaxLevel * 2) & 0xFF)) << 19;
    data |= ((uint64_t)((level_right + kAttenOffMaxLevel * 2) & 0xFF)) << 27;
    data |= ((uint64_t)(mix ? 1 : 0)) << 35;
    return data;
}
```

- [ ] **Step 5: Add per-applet setter helper**

In the anon namespace of `harness/tests/test_hemispheres.cpp`:

```cpp
void atten_off_set(hem_shim::HemispheresInstance* hi,
                    int offset_left, int offset_right,
                    int level_left, int level_right,
                    bool mix = false) {
    get_applet(hi, LEFT)->OnDataReceive(
        pack_atten_off(offset_left, offset_right, level_left, level_right, mix));
}
```

- [ ] **Step 6: Add TEST_CASEs**

Append:

```cpp
TEST_CASE("atten_off A1: Start defaults level[0]=level[1]=100, offset=0", "[atten_off]") {
    auto s = setup_applet(kAppletAttenuateOffset);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int off0 = (int)((packed) & 0x1FF) - 256;
    int off1 = (int)((packed >> 10) & 0x1FF) - 256;
    int lev0 = (int)((packed >> 19) & 0xFF) - 200;  // ATTENOFF_MAX_LEVEL*2 bias
    int lev1 = (int)((packed >> 27) & 0xFF) - 200;
    bool mix = ((packed >> 35) & 0x1) != 0;
    REQUIRE(off0 == 0);
    REQUIRE(off1 == 0);
    REQUIRE(lev0 == 100);
    REQUIRE(lev1 == 100);
    REQUIRE(mix == false);
}

TEST_CASE("atten_off A2: unity gain passes signal through", "[atten_off]") {
    auto s = setup_applet(kAppletAttenuateOffset);
    atten_off_set(s.hi, 0, 0, 100, 100, false);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 2.0f, 8);
    set_cv(s.bus, LEFT, 1, 3.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.05f));
    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(3.0f).margin(0.05f));
}

TEST_CASE("atten_off A3: 50% level halves signal", "[atten_off]") {
    auto s = setup_applet(kAppletAttenuateOffset);
    atten_off_set(s.hi, 0, 0, 50, 50, false);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 4.0f, 8);
    set_cv(s.bus, LEFT, 1, 4.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.05f));
    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(2.0f).margin(0.05f));
}

TEST_CASE("atten_off A4: -100% level inverts signal", "[atten_off]") {
    auto s = setup_applet(kAppletAttenuateOffset);
    atten_off_set(s.hi, 0, 0, -100, -100, false);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 2.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(-2.0f).margin(0.05f));
}

TEST_CASE("atten_off A5: positive offset shifts signal up", "[atten_off]") {
    // Offset is in semitones (ATTENOFF_INCREMENTS = ONE_OCTAVE / 12 = 128 hem units).
    // offset=12 -> +1V shift.
    auto s = setup_applet(kAppletAttenuateOffset);
    atten_off_set(s.hi, 12, 0, 100, 100, false);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 1.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.05f));
}

TEST_CASE("atten_off A6: output clamps at HEMISPHERE_MAX_CV", "[atten_off]") {
    auto s = setup_applet(kAppletAttenuateOffset);
    // 100% gain, offset +12 semitones (=1V); input 6V; signal 7V before clamp; expected 6V.
    atten_off_set(s.hi, 12, 0, 100, 100, false);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 6.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(6.0f).margin(0.05f));
}

TEST_CASE("atten_off A7: mix mode sums Out(0) into Out(1)", "[atten_off]") {
    auto s = setup_applet(kAppletAttenuateOffset);
    // Out(0) computes signal_0 = 100% * 1V + 0 = 1V. Out(1) computes signal_1 =
    // 100% * 2V + 0 = 2V. With mix=true, Out(1) becomes signal_0 + signal_1 = 3V.
    atten_off_set(s.hi, 0, 0, 100, 100, true);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 1.0f, 8);
    set_cv(s.bus, LEFT, 1, 2.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(1.0f).margin(0.05f));
    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(3.0f).margin(0.05f));
}

TEST_CASE("atten_off A8: serialise round-trip preserves all fields", "[atten_off]") {
    auto s = setup_applet(kAppletAttenuateOffset);
    atten_off_set(s.hi, -12, 24, -50, 150, true);

    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int off0 = (int)((packed) & 0x1FF) - 256;
    int off1 = (int)((packed >> 10) & 0x1FF) - 256;
    int lev0 = (int)((packed >> 19) & 0xFF) - 200;
    int lev1 = (int)((packed >> 27) & 0xFF) - 200;
    bool mix = ((packed >> 35) & 0x1) != 0;

    REQUIRE(off0 == -12);
    REQUIRE(off1 == 24);
    REQUIRE(lev0 == -50);
    REQUIRE(lev1 == 150);
    REQUIRE(mix == true);
}
```

- [ ] **Step 7: Run tests**

```bash
./build/host/test_hemispheres "[atten_off]"
```

Expected: 8 cases pass.

If `ATTENOFF_INCREMENTS` is not 128 (1 semitone = ONE_OCTAVE/12), adjust A5 accordingly using the actual conversion ratio.

- [ ] **Step 8: Commit**

```bash
git add harness/tests/applet_test_helpers.h harness/tests/applet_test_helpers.cpp harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): atten_off A1-A8 level + offset + mix"
```

---

## Task 4: Slew (Tier 1)

**Files:**
- Modify: `harness/tests/applet_test_helpers.h` (add `pack_slew` decl)
- Modify: `harness/tests/applet_test_helpers.cpp` (add `pack_slew` impl)
- Modify: `harness/tests/test_hemispheres.cpp` (add TEST_CASEs)

Vendor source: `vendor/O_C-Phazerville/software/src/applets/Slew.h`.

Slew is a per-channel rate-limited follower. `rise` and `fall` are in [0, HEM_SLEW_MAX_VALUE] (HEM_SLEW_MAX_VALUE is a vendor constant; look it up). Internal state is `simfloat signal[2]` updated each Controller iteration. `Gate(ch)` high defeats slew: signal jumps immediately to In(ch).

Bit layout (spec table, 16 bits): `Pack(data, {0,8}, rise); Pack(data, {8,8}, fall);`.

Default state: `signal[0] = signal[1] = 0`, `rise = fall = 50`.

Slew tests are time-sensitive. Use multi-step scenarios; signal evolves over many `step_n_frames` calls. Allow generous margins (0.5V) because the simfloat path has accumulated rounding.

- [ ] **Step 1: Re-verify constants**

```bash
grep -n 'HEM_SLEW_MAX_VALUE\|HEM_SLEW_MAX_TICKS\|simfloat' vendor/O_C-Phazerville/software/src/applets/Slew.h shim/include/HSUtils.h shim/include/HemisphereApplet.h
```

Note the constants. `HEM_SLEW_MAX_VALUE` is likely 100 (matching the encoder UI range).

- [ ] **Step 2: Add `pack_slew` declaration**

```cpp
// Mirrors Slew::OnDataRequest: bits [0,8] = rise (0..HEM_SLEW_MAX_VALUE), [8,8] = fall.
uint64_t pack_slew(int rise, int fall);
```

- [ ] **Step 3: Add implementation**

```cpp
uint64_t pack_slew(int rise, int fall) {
    return ((uint64_t)(rise & 0xFF))
         | ((uint64_t)(fall & 0xFF) << 8);
}
```

- [ ] **Step 4: Add per-applet setter**

```cpp
void slew_set(hem_shim::HemispheresInstance* hi, int rise, int fall) {
    get_applet(hi, LEFT)->OnDataReceive(pack_slew(rise, fall));
}
```

- [ ] **Step 5: Add TEST_CASEs**

```cpp
TEST_CASE("slew SL1: Start defaults rise=50, fall=50", "[slew]") {
    auto s = setup_applet(kAppletSlew);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF)        == 50);
    REQUIRE(((packed >> 8) & 0xFF) == 50);
}

TEST_CASE("slew SL2: zero rise/fall is instant follower", "[slew]") {
    auto s = setup_applet(kAppletSlew);
    slew_set(s.hi, 0, 0);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 3.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(3.0f).margin(0.5f));
}

TEST_CASE("slew SL3: gate defeats slew", "[slew]") {
    // Vendor Slew.h:35: if (Gate(ch)) signal[ch] = input. Instant jump.
    auto s = setup_applet(kAppletSlew);
    slew_set(s.hi, 100, 100);  // max slew (slow)

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 3.0f, 8);
    hold_gate(s.bus, LEFT, 0, 8);   // defeat
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(3.0f).margin(0.5f));
}

TEST_CASE("slew SL4: high rise slows attack to target", "[slew]") {
    auto s = setup_applet(kAppletSlew);
    slew_set(s.hi, 100, 100);

    // Single step at the target: output should be far below the input
    // because slew is at maximum.
    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 5.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) < 4.5f);

    // After enough steps, output approaches the target.
    for (int i = 0; i < 200; ++i) {
        clear_bus(s.bus);
        set_cv(s.bus, LEFT, 0, 5.0f, 8);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
    }
    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(5.0f).margin(0.5f));
}

TEST_CASE("slew SL5: serialise round-trip preserves rise/fall", "[slew]") {
    auto s = setup_applet(kAppletSlew);
    slew_set(s.hi, 17, 83);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF)        == 17);
    REQUIRE(((packed >> 8) & 0xFF) == 83);
}
```

- [ ] **Step 6: Run tests**

```bash
./build/host/test_hemispheres "[slew]"
```

Expected: 5 cases pass.

Slew is timing-sensitive. If SL4's "200 steps" isn't enough, increase to 500. If margins fail, adjust to the next coarser tier (1.0V) only after confirming the slew formula matches vendor.

- [ ] **Step 7: Commit**

```bash
git add harness/tests/applet_test_helpers.h harness/tests/applet_test_helpers.cpp harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): slew SL1-SL5 rate-limited follower + defeat"
```

---

## Task 5: Burst (Tier 1)

**Files:**
- Modify: `harness/tests/applet_test_helpers.h` (add `pack_burst`)
- Modify: `harness/tests/applet_test_helpers.cpp` (impl)
- Modify: `harness/tests/test_hemispheres.cpp` (TEST_CASEs)

Vendor source: `vendor/O_C-Phazerville/software/src/applets/Burst.h`.

Burst is a clock-driven burst generator. Settings: `number` (1..HEM_BURST_NUMBER_MAX), `spacing` (HEM_BURST_SPACING_MIN..MAX), `div` (-8..N), `jitter`, `accel`. Free-run mode if no Clock(0) ever fires; clocked mode after first Clock(0). Uses Modulate (CV1 modulates number; CV2 modulates spacing in unclocked mode).

Bit layout (spec table, 40 bits): number (0,8), spacing (8,8), div+8 (16,8), jitter (24,8), accel (32,8).

Burst is complex. Focus tests on observable behaviors: Start defaults, burst fires Clock(1) gate output, number controls count, serialise round-trip. Don't try to test internal timing math comprehensively.

- [ ] **Step 1: Re-verify bit layout and constants**

```bash
awk '/uint64_t OnDataRequest|OnDataReceive\(uint64_t/,/^[ \t]*}/' vendor/O_C-Phazerville/software/src/applets/Burst.h
grep -n 'HEM_BURST' vendor/O_C-Phazerville/software/src/applets/Burst.h shim/include/HSUtils.h
```

Note the constants. `HEM_BURST_NUMBER_MAX` defines the max burst count (likely 16 or 32). `HEM_BURST_SPACING_MIN/MAX` are spacing bounds in ms.

- [ ] **Step 2: Add `pack_burst`**

```cpp
// Mirrors Burst::OnDataRequest:
// number (0,8) | spacing (8,8) | div+8 (16,8) | jitter (24,8) | accel (32,8).
// div is biased by +8 in the wire format.
uint64_t pack_burst(int number, int spacing, int div, int jitter, int accel);
```

```cpp
uint64_t pack_burst(int number, int spacing, int div, int jitter, int accel) {
    uint64_t data = 0;
    data |= ((uint64_t)(number   & 0xFF));
    data |= ((uint64_t)(spacing  & 0xFF)) << 8;
    data |= ((uint64_t)((div + 8) & 0xFF)) << 16;
    data |= ((uint64_t)(jitter   & 0xFF)) << 24;
    data |= ((uint64_t)(accel    & 0xFF)) << 32;
    return data;
}
```

- [ ] **Step 3: Add per-applet setter**

```cpp
void burst_set(hem_shim::HemispheresInstance* hi,
               int number, int spacing, int div, int jitter, int accel) {
    get_applet(hi, LEFT)->OnDataReceive(
        pack_burst(number, spacing, div, jitter, accel));
}
```

- [ ] **Step 4: Add TEST_CASEs**

```cpp
TEST_CASE("burst B1: Start defaults number=4, spacing=50, div=1, jitter=0, accel=0", "[burst]") {
    auto s = setup_applet(kAppletBurst);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int number  = (int)((packed) & 0xFF);
    int spacing = (int)((packed >> 8) & 0xFF);
    int div     = (int)((packed >> 16) & 0xFF) - 8;
    int jitter  = (int)((packed >> 24) & 0xFF);
    int accel   = (int)((packed >> 32) & 0xFF);
    REQUIRE(number  == 4);
    REQUIRE(spacing == 50);
    REQUIRE(div     == 1);
    REQUIRE(jitter  == 0);
    REQUIRE(accel   == 0);
}

TEST_CASE("burst B2: Clock(0) fires a burst that produces gate pulses on output 0", "[burst]") {
    // After a Clock(0) edge, Burst emits `number` pulses on output 0 at `spacing`
    // intervals. Run for many steps after the clock; assert that output 0 went
    // high at least once.
    auto s = setup_applet(kAppletBurst);
    burst_set(s.hi, 2, 50, 1, 0, 0);  // 2 pulses, default spacing
    seed_hem_rng(0xDEADBEEF);

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);  // Clock(0) edge
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    bool saw_pulse = read_gate_at(s.bus, LEFT, 0, 0, 8);
    // The burst may fire on a later step; advance many buffers and watch
    // for the gate output going high.
    for (int i = 0; i < 100 && !saw_pulse; ++i) {
        clear_bus(s.bus);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
        if (read_gate_at(s.bus, LEFT, 0, 0, 8)) saw_pulse = true;
    }
    REQUIRE(saw_pulse);
}

TEST_CASE("burst B3: serialise round-trip preserves all fields", "[burst]") {
    auto s = setup_applet(kAppletBurst);
    burst_set(s.hi, 7, 100, -3, 25, 10);

    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int number  = (int)((packed) & 0xFF);
    int spacing = (int)((packed >> 8) & 0xFF);
    int div     = (int)((packed >> 16) & 0xFF) - 8;
    int jitter  = (int)((packed >> 24) & 0xFF);
    int accel   = (int)((packed >> 32) & 0xFF);
    REQUIRE(number  == 7);
    REQUIRE(spacing == 100);
    REQUIRE(div     == -3);
    REQUIRE(jitter  == 25);
    REQUIRE(accel   == 10);
}
```

- [ ] **Step 5: Run tests**

```bash
./build/host/test_hemispheres "[burst]"
```

Expected: 3 cases pass.

If B2 doesn't observe a gate pulse, the burst may need more time. Increase the loop iteration count to 500 or 1000. Burst's internal `ticks_since_clock` is in vendor ticks (not buffer steps); `Controller()` runs 10x per buffer step, so 1000 buffer iterations = 10,000 ticks.

- [ ] **Step 6: Commit**

```bash
git add harness/tests/applet_test_helpers.h harness/tests/applet_test_helpers.cpp harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): burst B1-B3 defaults + clocked burst + serialise"
```

---

## Task 6: Compare (Tier 2)

**Files:**
- Modify: `harness/tests/applet_test_helpers.h` (add `pack_compare`)
- Modify: `harness/tests/applet_test_helpers.cpp` (impl)
- Modify: `harness/tests/test_hemispheres.cpp` (TEST_CASEs)

Vendor source: `vendor/O_C-Phazerville/software/src/applets/Compare.h`.

Compare is single-knob CV threshold. `level` is in [0, HEM_COMPARE_MAX_VALUE] (probably 256). Threshold is `Proportion(level, HEM_COMPARE_MAX_VALUE, HEMISPHERE_MAX_CV)`. CV2 (`DetentedIn(1)`) modulates the threshold up/down. Output: GateOut(0, 1) if In(0) > mod_cv, else GateOut(1, 1).

Bit layout (spec table, 8 bits): `Pack(data, {0,8}, level)`.

Default: level = 128, mod_cv = 0.

- [ ] **Step 1: Re-verify**

```bash
awk '/uint64_t OnDataRequest|OnDataReceive\(uint64_t/,/^[ \t]*}/' vendor/O_C-Phazerville/software/src/applets/Compare.h
grep -n 'HEM_COMPARE' vendor/O_C-Phazerville/software/src/applets/Compare.h shim/include/HSUtils.h
```

- [ ] **Step 2: Add `pack_compare`**

```cpp
// Mirrors Compare::OnDataRequest: bits [0,8] = level (0..HEM_COMPARE_MAX_VALUE).
uint64_t pack_compare(int level);
```

```cpp
uint64_t pack_compare(int level) {
    return (uint64_t)(level & 0xFF);
}
```

- [ ] **Step 3: Add per-applet setter**

```cpp
void compare_set(hem_shim::HemispheresInstance* hi, int level) {
    get_applet(hi, LEFT)->OnDataReceive(pack_compare(level));
}
```

- [ ] **Step 4: Add TEST_CASEs**

```cpp
TEST_CASE("compare CM1: Start defaults level=128", "[compare]") {
    auto s = setup_applet(kAppletCompare);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF) == 128);
}

TEST_CASE("compare CM2: In(0) above threshold drives output 0 high", "[compare]") {
    // level=128 -> Proportion(128, 256, HEM_MAX_CV) -> ~half of 6V = 3V.
    auto s = setup_applet(kAppletCompare);
    compare_set(s.hi, 128);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 4.0f, 8);  // above threshold
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == false);
}

TEST_CASE("compare CM3: In(0) below threshold drives output 1 high", "[compare]") {
    auto s = setup_applet(kAppletCompare);
    compare_set(s.hi, 128);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 2.0f, 8);  // below threshold
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == true);
}

TEST_CASE("compare CM4: serialise round-trip", "[compare]") {
    auto s = setup_applet(kAppletCompare);
    compare_set(s.hi, 200);
    REQUIRE((get_applet(s.hi, LEFT)->OnDataRequest() & 0xFF) == 200);
}
```

- [ ] **Step 5: Run tests**

```bash
./build/host/test_hemispheres "[compare]"
```

Expected: 4 cases pass.

- [ ] **Step 6: Commit**

```bash
git add harness/tests/applet_test_helpers.h harness/tests/applet_test_helpers.cpp harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): compare CM1-CM4 threshold + serialise"
```

---

## Task 7: GatedVCA (Tier 2, zero-state-serialise)

**Files:**
- Modify: `harness/tests/test_hemispheres.cpp` (TEST_CASEs only; no pack helper)

Vendor source: `vendor/O_C-Phazerville/software/src/applets/GatedVCA.h`.

GatedVCA has two outputs. Output 0 is normally off: only emits CV when Gate(0) is high. Output 1 is normally on: emits CV unless Gate(1) is high (mute). VCA scales `signal = In(0)` by `amplitude = In(1) + amp_offset_cv`, where `amp_offset_cv` is internal state set via encoder.

Bit layout: 0 bits. `OnDataRequest` returns 0; `OnDataReceive` ignores input. Preset save/load resets state. Tests inject internal state via `OnEncoderMove`.

Default state: `amp_offset_pct = 0`, `amp_offset_cv = 0`.

- [ ] **Step 1: Re-verify**

```bash
awk '/uint64_t OnDataRequest|OnDataReceive\(uint64_t/,/^[ \t]*}/' vendor/O_C-Phazerville/software/src/applets/GatedVCA.h
```

Confirm `OnDataRequest` returns 0 and `OnDataReceive` is a no-op. If those facts have changed, add a `pack_gated_vca` helper accordingly. This task assumes the spec table is still accurate.

- [ ] **Step 2: Add TEST_CASEs**

```cpp
TEST_CASE("gated_vca GV1: Out(0) is zero when Gate(0) is low", "[gated_vca]") {
    auto s = setup_applet(kAppletGatedVCA);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 3.0f, 8);  // signal
    set_cv(s.bus, LEFT, 1, 6.0f, 8);  // amplitude = max
    // No gate written.
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(0.0f).margin(0.05f));
}

TEST_CASE("gated_vca GV2: Out(0) passes signal scaled by amplitude when Gate(0) is high", "[gated_vca]") {
    // Vendor formula: output = Proportion(amplitude, HEMISPHERE_MAX_INPUT_CV, signal).
    // amplitude = HEMISPHERE_MAX_INPUT_CV (6V) -> output = signal (1:1).
    auto s = setup_applet(kAppletGatedVCA);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 3.0f, 8);  // signal
    set_cv(s.bus, LEFT, 1, 6.0f, 8);  // amplitude (full)
    hold_gate(s.bus, LEFT, 0, 8);     // gate open
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(3.0f).margin(0.1f));
}

TEST_CASE("gated_vca GV3: Out(1) is normally-on (passes signal unless Gate(1) mutes)", "[gated_vca]") {
    auto s = setup_applet(kAppletGatedVCA);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 3.0f, 8);
    set_cv(s.bus, LEFT, 1, 6.0f, 8);
    // No gate on either input.
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(3.0f).margin(0.1f));
}

TEST_CASE("gated_vca GV4: Out(1) mutes when Gate(1) is high", "[gated_vca]") {
    auto s = setup_applet(kAppletGatedVCA);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 3.0f, 8);
    set_cv(s.bus, LEFT, 1, 6.0f, 8);
    hold_gate(s.bus, LEFT, 1, 8);  // mute
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 1, 0, 8) == Approx(0.0f).margin(0.05f));
}

TEST_CASE("gated_vca GV5: half amplitude halves signal", "[gated_vca]") {
    auto s = setup_applet(kAppletGatedVCA);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 4.0f, 8);  // signal
    set_cv(s.bus, LEFT, 1, 3.0f, 8);  // amplitude = half of max
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_cv_at(s.bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.1f));
}

TEST_CASE("gated_vca GV6: serialise is no-op (returns 0)", "[gated_vca]") {
    auto s = setup_applet(kAppletGatedVCA);
    REQUIRE(get_applet(s.hi, LEFT)->OnDataRequest() == 0);
}
```

- [ ] **Step 3: Run tests**

```bash
./build/host/test_hemispheres "[gated_vca]"
```

Expected: 6 cases pass.

- [ ] **Step 4: Commit**

```bash
git add harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): gated_vca GV1-GV6 normally-off + normally-on"
```

---

## Task 8: Button (Tier 2, zero-state-serialise)

**Files:**
- Modify: `harness/tests/test_hemispheres.cpp` (TEST_CASEs only)

Vendor source: `vendor/O_C-Phazerville/software/src/applets/Button.h`.

Button has two channels, each in one of two modes: trigger (default) or gate-toggle. Encoder button press emits a trigger on the corresponding output. A physical clock on the input (Clock(ch, 1)) also presses the button. In gate mode, the output toggles state per press.

Bit layout: 0 bits (no state serialised). Internal state: `trigger_out[2]`, `toggle_st[2]`, `gate_mode[2]`, `trigger_countdown`. Default: `trigger_countdown = 0`; gate_mode and toggle_st default to 0.

Vendor `Controller()` reads `Clock(ch, 1)` to detect physical input and calls `PressButton(ch)`. `PressButton(ch)` is a private method that sets `trigger_out[ch] = 1` and toggles `toggle_st[ch]` if `gate_mode[ch]`.

Button tests inject state via UI methods. The encoder cursor controls which channel is selected. The button press is `OnButtonPress()`. Mode toggling is via encoder press (i.e., separate function from the channel-press) or via `OnEncoderMove`.

Read the Button source carefully before writing tests to understand which UI events produce which outputs.

- [ ] **Step 1: Re-verify**

```bash
cat vendor/O_C-Phazerville/software/src/applets/Button.h
```

Specifically inspect:
- `Start()`
- `Controller()`
- `OnButtonPress()`
- `OnEncoderMove(int)`
- `PressButton(int ch)` (private)

Map the UI semantics: which button press maps to which output? Is there a way to switch between trigger and gate mode programmatically without going through the encoder cursor?

- [ ] **Step 2: Add TEST_CASEs**

```cpp
TEST_CASE("button BT1: Start leaves outputs low", "[button]") {
    auto s = setup_applet(kAppletButton);

    clear_bus(s.bus);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == false);
}

TEST_CASE("button BT2: physical clock on input 0 produces output 0 trigger", "[button]") {
    // Vendor Controller: Clock(ch, 1) reads digital input directly. Use set_gate
    // for a single-sample rising edge; subsequent buffer fires Controller's
    // PressButton(0) which sets trigger_out[0] = 1; next Controller pass emits
    // ClockOut on output 0.
    auto s = setup_applet(kAppletButton);

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);  // physical Clock on channel 0
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    bool saw_pulse = read_gate_at(s.bus, LEFT, 0, 0, 8);
    // The trigger may not fire on the same step; advance more if needed.
    for (int i = 0; i < 10 && !saw_pulse; ++i) {
        clear_bus(s.bus);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
        if (read_gate_at(s.bus, LEFT, 0, 0, 8)) saw_pulse = true;
    }
    REQUIRE(saw_pulse);
}

TEST_CASE("button BT3: serialise is no-op (returns 0)", "[button]") {
    auto s = setup_applet(kAppletButton);
    REQUIRE(get_applet(s.hi, LEFT)->OnDataRequest() == 0);
}
```

If exploration of the source reveals more testable behaviors (e.g., gate-mode toggle, OnButtonPress effect on the cursor channel), add additional cases. Phase 2 review will catch undercoverage.

- [ ] **Step 3: Run tests**

```bash
./build/host/test_hemispheres "[button]"
```

Expected: at least 3 cases pass.

- [ ] **Step 4: Commit**

```bash
git add harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): button BT1-BT3 trigger + serialise no-op"
```

---

## Task 9: ClkToGate (Tier 2)

**Files:**
- Modify: `harness/tests/applet_test_helpers.h` (add `pack_clk_to_gate`)
- Modify: `harness/tests/applet_test_helpers.cpp` (impl)
- Modify: `harness/tests/test_hemispheres.cpp` (TEST_CASEs)

Vendor source: `vendor/O_C-Phazerville/software/src/applets/ClkToGate.h`.

ClkToGate converts each input clock to a width-controlled gate. Per side: `width[ch]` (1..100 percent), `range[ch]` (-99..99 random width range), `skip[ch]` (0..100 percent chance to skip a clock). Width 100% produces a tied (sustained) gate. Uses Modulate for width.

Bit layout (spec table, 64 bits): per side i in {0,1}: width[i] at `(i*32, 7)`, abs(range[i]) at `(i*32+8, 7)`, range[i] sign at `(i*32+15, 1)`, skip[i] at `(i*32+16, 7)`. Fills exactly 64 bits.

Default: `width[0]=25, width[1]=50`; `range[0]=0, range[1]=25`; `skip[ch]=0`.

- [ ] **Step 1: Re-verify**

```bash
awk '/uint64_t OnDataRequest|OnDataReceive\(uint64_t/,/^[ \t]*}/' vendor/O_C-Phazerville/software/src/applets/ClkToGate.h
```

Confirm the per-side packing layout. The vendor uses a loop over `i` with `i*32` offsets.

- [ ] **Step 2: Add `pack_clk_to_gate`**

```cpp
// Mirrors ClkToGate::OnDataRequest: per side i in {0,1}:
//   width[i] at (i*32+0, 7)
//   abs(range[i]) at (i*32+8, 7)
//   range[i] sign at (i*32+15, 1)
//   skip[i] at (i*32+16, 7)
uint64_t pack_clk_to_gate(int width_a, int range_a, int skip_a,
                            int width_b, int range_b, int skip_b);
```

```cpp
uint64_t pack_clk_to_gate(int width_a, int range_a, int skip_a,
                            int width_b, int range_b, int skip_b) {
    auto pack_side = [](int width, int range, int skip) -> uint64_t {
        uint64_t side = 0;
        side |= ((uint64_t)(width & 0x7F));
        side |= ((uint64_t)((range < 0 ? -range : range) & 0x7F)) << 8;
        side |= ((uint64_t)(range < 0 ? 1 : 0)) << 15;
        side |= ((uint64_t)(skip & 0x7F)) << 16;
        return side;
    };
    uint64_t data = 0;
    data |= pack_side(width_a, range_a, skip_a);
    data |= pack_side(width_b, range_b, skip_b) << 32;
    return data;
}
```

- [ ] **Step 3: Add per-applet setter**

```cpp
void clk_to_gate_set(hem_shim::HemispheresInstance* hi,
                       int width_a, int range_a, int skip_a,
                       int width_b, int range_b, int skip_b) {
    get_applet(hi, LEFT)->OnDataReceive(
        pack_clk_to_gate(width_a, range_a, skip_a, width_b, range_b, skip_b));
}
```

- [ ] **Step 4: Add TEST_CASEs**

```cpp
TEST_CASE("clk_to_gate CG1: Start defaults width=25/50, range=0/25, skip=0/0", "[clk_to_gate]") {
    auto s = setup_applet(kAppletClkToGate);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int w0 = (int)((packed) & 0x7F);
    int r0_abs = (int)((packed >> 8) & 0x7F);
    int r0_sign = (int)((packed >> 15) & 0x1);
    int sk0 = (int)((packed >> 16) & 0x7F);
    int w1 = (int)((packed >> 32) & 0x7F);
    int r1_abs = (int)((packed >> 40) & 0x7F);
    int r1_sign = (int)((packed >> 47) & 0x1);
    int sk1 = (int)((packed >> 48) & 0x7F);
    REQUIRE(w0 == 25);
    REQUIRE(r0_abs == 0); REQUIRE(r0_sign == 0);
    REQUIRE(sk0 == 0);
    REQUIRE(w1 == 50);
    REQUIRE(r1_abs == 25); REQUIRE(r1_sign == 0);
    REQUIRE(sk1 == 0);
}

TEST_CASE("clk_to_gate CG2: Clock(0) produces a gate output", "[clk_to_gate]") {
    auto s = setup_applet(kAppletClkToGate);
    clk_to_gate_set(s.hi, 50, 0, 0,  50, 0, 0);
    seed_hem_rng(0xDEADBEEF);

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);  // Clock(0) edge
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    bool saw_pulse = read_gate_at(s.bus, LEFT, 0, 0, 8);
    for (int i = 0; i < 10 && !saw_pulse; ++i) {
        clear_bus(s.bus);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
        if (read_gate_at(s.bus, LEFT, 0, 0, 8)) saw_pulse = true;
    }
    REQUIRE(saw_pulse);
}

TEST_CASE("clk_to_gate CG3: width=100 produces tied gate", "[clk_to_gate]") {
    // Vendor: width_mod == 100 -> GateOut(ch, 1) (sustained, not ClockOut).
    auto s = setup_applet(kAppletClkToGate);
    clk_to_gate_set(s.hi, 100, 0, 0,  50, 0, 0);
    seed_hem_rng(0xDEADBEEF);

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);

    // Advance many steps; output should still be high (tied).
    for (int i = 0; i < 50; ++i) {
        clear_bus(s.bus);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
    }
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
}

TEST_CASE("clk_to_gate CG4: skip=100 always skips the clock", "[clk_to_gate]") {
    auto s = setup_applet(kAppletClkToGate);
    clk_to_gate_set(s.hi, 50, 0, 100,  50, 0, 0);
    seed_hem_rng(0xDEADBEEF);

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    // Output stays low across many steps (every clock is skipped).
    for (int i = 0; i < 20; ++i) {
        REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
        clear_bus(s.bus);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
    }
}

TEST_CASE("clk_to_gate CG5: serialise round-trip preserves all per-side fields", "[clk_to_gate]") {
    auto s = setup_applet(kAppletClkToGate);
    clk_to_gate_set(s.hi, 17, -23, 5,  77, 50, 99);

    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int w0 = (int)((packed) & 0x7F);
    int r0_abs = (int)((packed >> 8) & 0x7F);
    int r0_sign = (int)((packed >> 15) & 0x1);
    int sk0 = (int)((packed >> 16) & 0x7F);
    int w1 = (int)((packed >> 32) & 0x7F);
    int r1_abs = (int)((packed >> 40) & 0x7F);
    int r1_sign = (int)((packed >> 47) & 0x1);
    int sk1 = (int)((packed >> 48) & 0x7F);
    REQUIRE(w0 == 17);
    REQUIRE(r0_abs == 23); REQUIRE(r0_sign == 1);  // sign bit set means negative
    REQUIRE(sk0 == 5);
    REQUIRE(w1 == 77);
    REQUIRE(r1_abs == 50); REQUIRE(r1_sign == 0);
    REQUIRE(sk1 == 99);
}
```

- [ ] **Step 5: Run tests**

```bash
./build/host/test_hemispheres "[clk_to_gate]"
```

Expected: 5 cases pass.

- [ ] **Step 6: Commit**

```bash
git add harness/tests/applet_test_helpers.h harness/tests/applet_test_helpers.cpp harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): clk_to_gate CG1-CG5 width + skip + serialise"
```

---

## Task 10: GateDelay (Tier 2)

**Files:**
- Modify: `harness/tests/applet_test_helpers.h` (add `pack_gate_delay`)
- Modify: `harness/tests/applet_test_helpers.cpp` (impl)
- Modify: `harness/tests/test_hemispheres.cpp` (TEST_CASEs)

Vendor source: `vendor/O_C-Phazerville/software/src/applets/GateDelay.h`.

GateDelay maintains a 64 * 32-bit circular buffer per channel and reads playback head `time[ch]` ms behind the record head. `Controller()` decrements an `ms_countdown` and only runs the body once per millisecond (every ~16 ticks). Time is per channel in [0, 2000] ms.

Bit layout (spec table, 22 bits): `Pack(data, {0,11}, time[0]); Pack(data, {11,11}, time[1]);`.

Default: `time[ch] = 1000` (1 second).

GateDelay timing tests are slow because of the 1ms throttle. Use short delay times (e.g., 50ms) and many step iterations.

- [ ] **Step 1: Re-verify**

```bash
awk '/uint64_t OnDataRequest|OnDataReceive\(uint64_t/,/^[ \t]*}/' vendor/O_C-Phazerville/software/src/applets/GateDelay.h
```

- [ ] **Step 2: Add `pack_gate_delay`**

```cpp
// Mirrors GateDelay::OnDataRequest: bits [0,11] = time[0], [11,11] = time[1].
uint64_t pack_gate_delay(int time_left, int time_right);
```

```cpp
uint64_t pack_gate_delay(int time_left, int time_right) {
    return ((uint64_t)(time_left  & 0x7FF))
         | ((uint64_t)(time_right & 0x7FF) << 11);
}
```

- [ ] **Step 3: Add per-applet setter**

```cpp
void gate_delay_set(hem_shim::HemispheresInstance* hi, int time_left, int time_right) {
    get_applet(hi, LEFT)->OnDataReceive(pack_gate_delay(time_left, time_right));
}
```

- [ ] **Step 4: Add TEST_CASEs**

```cpp
TEST_CASE("gate_delay GD1: Start defaults time[0]=time[1]=1000", "[gate_delay]") {
    auto s = setup_applet(kAppletGateDelay);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int t0 = (int)((packed) & 0x7FF);
    int t1 = (int)((packed >> 11) & 0x7FF);
    REQUIRE(t0 == 1000);
    REQUIRE(t1 == 1000);
}

TEST_CASE("gate_delay GD2: gate appears at output after configured delay", "[gate_delay]") {
    // 100ms delay. Controller body runs every ms (every 16 ticks), so 100ms
    // delay = 100 ms-tick iterations. 1 buffer step has 10 Controller iterations
    // and ~32/3 = 10 vendor ticks; so each buffer advances 10/16 = 0.625 ms of
    // controller-body time. For 100ms we need ~160 buffer steps.
    auto s = setup_applet(kAppletGateDelay);
    gate_delay_set(s.hi, 100, 100);

    // Send a single gate edge.
    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    // Output should NOT be high immediately.
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);

    // Run many idle buffers; output should go high eventually.
    bool saw_delayed = false;
    for (int i = 0; i < 500 && !saw_delayed; ++i) {
        clear_bus(s.bus);
        step_n_frames(s.loaded, s.alg, s.bus, 32);
        if (read_gate_at(s.bus, LEFT, 0, 0, 8)) saw_delayed = true;
    }
    REQUIRE(saw_delayed);
}

TEST_CASE("gate_delay GD3: serialise round-trip preserves both times", "[gate_delay]") {
    auto s = setup_applet(kAppletGateDelay);
    gate_delay_set(s.hi, 250, 750);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    REQUIRE(((int)(packed & 0x7FF)) == 250);
    REQUIRE(((int)((packed >> 11) & 0x7FF)) == 750);
}
```

- [ ] **Step 5: Run tests**

```bash
./build/host/test_hemispheres "[gate_delay]"
```

Expected: 3 cases pass.

If GD2 fails because the gate never reappears, GateDelay's tape-recorder mechanic may be more subtle than the simple delay model. Inspect the vendor `record()` and `play()` methods and adjust the test. Track A asserts behavior at the bus; if the delay actually works the gate must come out, just possibly with a different timing relationship.

- [ ] **Step 6: Commit**

```bash
git add harness/tests/applet_test_helpers.h harness/tests/applet_test_helpers.cpp harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): gate_delay GD1-GD3 delay + serialise"
```

---

## Task 11: TLNeuron (Tier 2)

**Files:**
- Modify: `harness/tests/applet_test_helpers.h` (add `pack_tlneuron`)
- Modify: `harness/tests/applet_test_helpers.cpp` (impl)
- Modify: `harness/tests/test_hemispheres.cpp` (TEST_CASEs)

Vendor source: `vendor/O_C-Phazerville/software/src/applets/TLNeuron.h`.

Threshold-logic neuron. Three dendrites: D0=Gate(0), D1=Gate(1), D2=In(0)>2.5V. Each dendrite has a signed weight in [-9, +9]. Threshold in [-27, +36] (range from packed-bit reading, bias is +27 so 6 bits hold 0..63 mapping to -27..+36). Axon (output) fires if `sum > threshold`.

Bit layout (spec table, 21 bits):
- dendrite_weight[0] (0,5) biased +9
- dendrite_weight[1] (5,5) biased +9
- dendrite_weight[2] (10,5) biased +9
- threshold (15,6) biased +27

Default: weights and threshold start at 0.

- [ ] **Step 1: Re-verify**

```bash
awk '/uint64_t OnDataRequest|OnDataReceive\(uint64_t/,/^[ \t]*}/' vendor/O_C-Phazerville/software/src/applets/TLNeuron.h
```

- [ ] **Step 2: Add `pack_tlneuron`**

```cpp
// Mirrors TLNeuron::OnDataRequest: per-dendrite weight (5 bits, +9 bias) at
// offsets 0/5/10; threshold (6 bits, +27 bias) at offset 15.
uint64_t pack_tlneuron(int w0, int w1, int w2, int threshold);
```

```cpp
uint64_t pack_tlneuron(int w0, int w1, int w2, int threshold) {
    uint64_t data = 0;
    data |= ((uint64_t)((w0 + 9) & 0x1F));
    data |= ((uint64_t)((w1 + 9) & 0x1F)) << 5;
    data |= ((uint64_t)((w2 + 9) & 0x1F)) << 10;
    data |= ((uint64_t)((threshold + 27) & 0x3F)) << 15;
    return data;
}
```

- [ ] **Step 3: Add per-applet setter**

```cpp
void tlneuron_set(hem_shim::HemispheresInstance* hi, int w0, int w1, int w2, int threshold) {
    get_applet(hi, LEFT)->OnDataReceive(pack_tlneuron(w0, w1, w2, threshold));
}
```

- [ ] **Step 4: Add TEST_CASEs**

```cpp
TEST_CASE("tlneuron TL1: Start defaults weights and threshold to 0", "[tlneuron]") {
    auto s = setup_applet(kAppletTLNeuron);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int w0 = (int)((packed) & 0x1F) - 9;
    int w1 = (int)((packed >> 5) & 0x1F) - 9;
    int w2 = (int)((packed >> 10) & 0x1F) - 9;
    int th = (int)((packed >> 15) & 0x3F) - 27;
    REQUIRE(w0 == 0);
    REQUIRE(w1 == 0);
    REQUIRE(w2 == 0);
    REQUIRE(th == 0);
}

TEST_CASE("tlneuron TL2: sum > threshold fires axon", "[tlneuron]") {
    // weights = (5, 5, 5), threshold = 4. With Gate(0) high and Gate(1) low and
    // In(0) below 2.5V, sum = 5; 5 > 4; output high.
    auto s = setup_applet(kAppletTLNeuron);
    tlneuron_set(s.hi, 5, 5, 5, 4);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == true);  // both outputs mirror
}

TEST_CASE("tlneuron TL3: sum below threshold does not fire", "[tlneuron]") {
    // weights = (5, 5, 5), threshold = 6. Gate(0) high alone gives sum = 5; 5 > 6 false.
    auto s = setup_applet(kAppletTLNeuron);
    tlneuron_set(s.hi, 5, 5, 5, 6);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
}

TEST_CASE("tlneuron TL4: CV dendrite contributes when In(0) > 2.5V", "[tlneuron]") {
    // weights = (5, 0, 5), threshold = 4. Without CV: only Gate(0). With CV>2.5V: +5.
    // Gate(0) low + CV high: sum = 5; 5 > 4; fire.
    auto s = setup_applet(kAppletTLNeuron);
    tlneuron_set(s.hi, 5, 0, 5, 4);

    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 3.0f, 8);  // above 2.5V threshold
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);

    // Now drop CV below threshold; output should go low.
    clear_bus(s.bus);
    set_cv(s.bus, LEFT, 0, 2.0f, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
}

TEST_CASE("tlneuron TL5: negative weight inhibits", "[tlneuron]") {
    // weights = (5, -5, 0), threshold = 0. Gate(0) alone: sum = 5; fire.
    // Gate(0) + Gate(1): sum = 5 - 5 = 0; not > 0; no fire.
    auto s = setup_applet(kAppletTLNeuron);
    tlneuron_set(s.hi, 5, -5, 0, 0);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);

    clear_bus(s.bus);
    hold_gate(s.bus, LEFT, 0, 8);
    hold_gate(s.bus, LEFT, 1, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == false);
}

TEST_CASE("tlneuron TL6: serialise round-trip preserves weights and threshold", "[tlneuron]") {
    auto s = setup_applet(kAppletTLNeuron);
    tlneuron_set(s.hi, -7, 8, -3, 15);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int w0 = (int)((packed) & 0x1F) - 9;
    int w1 = (int)((packed >> 5) & 0x1F) - 9;
    int w2 = (int)((packed >> 10) & 0x1F) - 9;
    int th = (int)((packed >> 15) & 0x3F) - 27;
    REQUIRE(w0 == -7);
    REQUIRE(w1 == 8);
    REQUIRE(w2 == -3);
    REQUIRE(th == 15);
}
```

- [ ] **Step 5: Run tests**

```bash
./build/host/test_hemispheres "[tlneuron]"
```

Expected: 6 cases pass.

- [ ] **Step 6: Commit**

```bash
git add harness/tests/applet_test_helpers.h harness/tests/applet_test_helpers.cpp harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): tlneuron TL1-TL6 weighted dendrites + threshold"
```

---

## Task 12: Cumulus (Tier 2, last applet)

**Files:**
- Modify: `harness/tests/applet_test_helpers.h` (add `pack_cumulus`)
- Modify: `harness/tests/applet_test_helpers.cpp` (impl)
- Modify: `harness/tests/test_hemispheres.cpp` (TEST_CASEs)

Vendor source: `vendor/O_C-Phazerville/software/src/applets/Cumulus.h`.

Cumulus is a stochastic byte accumulator. On Clock(0), it applies one of 5 operations (ADD, SUB, MULADD1, XOR_ROTL, SUB_ROTR) to `acc_register` using `b_constant`. On Clock(1), it randomizes `acc_register` to a byte. Each output emits a specific bit of `acc_register`.

Internal state:
- `accoperator` (3 bits in serialise): which op
- `b_constant` (4 bits): operand
- `outmode[0]` (4 bits): which acc_register bit to output on Out(0)
- `outmode[1]` (4 bits): which acc_register bit to output on Out(1)

Bit layout (spec table, 17 bits used):
- accoperator (0,3)
- b_constant (3,4)
- outmode[0] (7,4)
- gap bits 11..12 unused
- outmode[1] (13,4)

**CRITICAL (spec P2-4):** `pack_cumulus` must explicitly zero bits 11..12 to avoid stale state leaking through round-trip.

Default: `cursor=0, accoperator=ADD (=0), acc_register=0, b_constant=0`.

`Modulate` is used on both `b_constant_mod` (CV2) and `a_mod` (CV1 modulates outmode[0]).

- [ ] **Step 1: Re-verify**

```bash
awk '/uint64_t OnDataRequest|OnDataReceive\(uint64_t/,/^[ \t]*}/' vendor/O_C-Phazerville/software/src/applets/Cumulus.h
grep -n 'AccOperator\|ACC_MAX_B\|ACC_MIN_B' vendor/O_C-Phazerville/software/src/applets/Cumulus.h
```

- [ ] **Step 2: Add `pack_cumulus`**

```cpp
// Mirrors Cumulus::OnDataRequest:
//   accoperator (0,3) | b_constant (3,4) | outmode[0] (7,4) | gap (11..12 unused) | outmode[1] (13,4)
// IMPORTANT: bits 11..12 are unused in vendor packing; pack_cumulus explicitly
// zeros them to avoid stale state leaking through preset round-trip.
uint64_t pack_cumulus(int accoperator, int b_constant, int outmode_left, int outmode_right);
```

```cpp
uint64_t pack_cumulus(int accoperator, int b_constant, int outmode_left, int outmode_right) {
    uint64_t data = 0;
    data |= ((uint64_t)(accoperator   & 0x07));
    data |= ((uint64_t)(b_constant    & 0x0F)) << 3;
    data |= ((uint64_t)(outmode_left  & 0x0F)) << 7;
    // bits 11..12 left as 0
    data |= ((uint64_t)(outmode_right & 0x0F)) << 13;
    return data;
}
```

- [ ] **Step 3: Add per-applet setter**

```cpp
void cumulus_set(hem_shim::HemispheresInstance* hi,
                  int accoperator, int b_constant,
                  int outmode_left, int outmode_right) {
    get_applet(hi, LEFT)->OnDataReceive(
        pack_cumulus(accoperator, b_constant, outmode_left, outmode_right));
}
```

- [ ] **Step 4: Add TEST_CASEs**

```cpp
TEST_CASE("cumulus CU1: Start defaults accoperator=ADD=0, b_constant=0", "[cumulus]") {
    auto s = setup_applet(kAppletCumulus);
    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int op  = (int)((packed) & 0x07);
    int b   = (int)((packed >> 3) & 0x0F);
    REQUIRE(op == 0);  // ADD
    REQUIRE(b  == 0);
}

TEST_CASE("cumulus CU2: ADD op increases acc_register by b_constant on Clock(0)", "[cumulus]") {
    // ADD with b=1 and outmode[0]=0 (bit 0). After 1 clock: acc=1; bit 0 = 1. Out(0) high.
    auto s = setup_applet(kAppletCumulus);
    cumulus_set(s.hi, 0, 1, 0, 1);  // ADD, b=1, outmode[0]=bit0, outmode[1]=bit1

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);  // Clock(0)
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);   // acc bit 0 = 1
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == false);  // acc bit 1 = 0
}

TEST_CASE("cumulus CU3: SUB op decreases acc_register", "[cumulus]") {
    // Set acc to 3 via two ADDs, then switch op to SUB. b=1, outmode[0]=0 (bit 0).
    // Final acc after ADD,ADD,ADD,SUB starting from 0 with b=1:
    //   ADD: 0+1=1
    //   ADD: 1+1=2 (but each clock runs only one op pass on this step due to
    //                ms_countdown=0 path? need to verify)
    // For simplicity: just verify SUB produces a different acc than ADD with same input.
    auto s = setup_applet(kAppletCumulus);
    cumulus_set(s.hi, 1, 5, 1, 1);  // SUB, b=5, outmode[0]=bit1

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);
    // acc was 0; after SUB by 5: acc = -5 = 0xFB (when wrapped to byte).
    // bit 1 of 0xFB = 1.
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
}

TEST_CASE("cumulus CU4: outmode picks correct bit of acc_register", "[cumulus]") {
    auto s = setup_applet(kAppletCumulus);
    cumulus_set(s.hi, 0, 5, 0, 2);  // ADD, b=5, outmode[0]=bit0, outmode[1]=bit2

    clear_bus(s.bus);
    set_gate(s.bus, LEFT, 0, 0, 8);
    step_n_frames(s.loaded, s.alg, s.bus, 32);

    // acc = 0 + 5 = 5 = 0b101. bit 0 = 1, bit 2 = 1.
    REQUIRE(read_gate_at(s.bus, LEFT, 0, 0, 8) == true);
    REQUIRE(read_gate_at(s.bus, LEFT, 1, 0, 8) == true);
}

TEST_CASE("cumulus CU5: serialise round-trip leaves gap bits zeroed", "[cumulus]") {
    auto s = setup_applet(kAppletCumulus);
    cumulus_set(s.hi, 2, 7, 5, 11);

    uint64_t packed = get_applet(s.hi, LEFT)->OnDataRequest();
    int op  = (int)((packed) & 0x07);
    int b   = (int)((packed >> 3) & 0x0F);
    int om0 = (int)((packed >> 7) & 0x0F);
    int gap = (int)((packed >> 11) & 0x03);
    int om1 = (int)((packed >> 13) & 0x0F);

    REQUIRE(op  == 2);
    REQUIRE(b   == 7);
    REQUIRE(om0 == 5);
    REQUIRE(gap == 0);  // explicit gap-bit check
    REQUIRE(om1 == 11);
}
```

- [ ] **Step 5: Run tests**

```bash
./build/host/test_hemispheres "[cumulus]"
```

Expected: 5 cases pass.

- [ ] **Step 6: Commit**

```bash
git add harness/tests/applet_test_helpers.h harness/tests/applet_test_helpers.cpp harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): cumulus CU1-CU5 ADD/SUB/outmode + gap-bit guard"
```

---

## Task 13: Final verification gate

**Files:**
- None (verification only).

- [ ] **Step 1: Run the full suite**

```bash
make clean
git submodule update --init --recursive
make arm
make test
```

Expected: ARM clean. `make test` chains test-applets (now with all Phase 1 + Phase 2 cases) + YAML scenario. Final line:

```
All tests passed (N assertions in M test cases)
```

Expected case count: 23 (Phase 1) + 7 (Logic) + 8 (AttenuateOffset) + 5 (Slew) + 3 (Burst) + 4 (Compare) + 6 (GatedVCA) + 3 (Button) + 5 (ClkToGate) + 3 (GateDelay) + 6 (TLNeuron) + 5 (Cumulus) = **78 cases total**. Exact assertion count varies.

- [ ] **Step 2: Run tagged subsets**

```bash
for tag in smoke calculate brancher logic atten_off slew burst compare gated_vca button clk_to_gate gate_delay tlneuron cumulus; do
  echo "=== [$tag] ==="
  ./build/host/test_hemispheres "[$tag]" | tail -3
done
```

Expected: each tag reports its subset passing.

- [ ] **Step 3: Hardware sanity gate**

Build the ARM artifact and deploy to a real NT module to confirm Hemispheres still loads and all 13 non-Empty applets behave as before.

```bash
make arm
make deploy-sysex
```

On the NT module:

- Misc, Plug-ins, View info: confirm Hemispheres lists with no failure flag.
- Add Hemispheres to a preset. Cycle the left selector through all 13 applets (Empty, AttenuateOffset, Brancher, Burst, Button, Calculate, ClkToGate, Compare, Cumulus, GateDelay, GatedVCA, Logic, Slew, TLNeuron). Each should display without crash.
- Spot-check 2-3 applets that exercise CV inputs (e.g., AttenuateOffset, Compare, Slew, GatedVCA): patch a CV source to the corresponding input and confirm the output behaves as expected.

- [ ] **Step 4: No additional commit**

This task verifies prior commits. No file changes. Proceed to PR creation (or local merge) if hardware passes.

---

## Recap: Adding a New Applet Test (Future Authors)

This section is the durable instruction set for adding host-side logic tests for any future Hemispheres applet. Read it before extending `test_hemispheres.cpp`. It assumes Phase 1 and Phase 2 are merged and the helper API in `harness/tests/applet_test_helpers.{h,cpp}` is in place.

### Mental model

The test harness links `applets/Hemispheres.cpp` directly into a Catch2 host binary. Each test allocates a `HemispheresInstance`, calls `Hemispheres_construct`, selects an applet via the left or right selector parameter, drives the bus with helper setters, calls `Hemispheres_step` to run `Controller()`, then asserts on the output bus or on packed state read back via `OnDataRequest`. There is no hardware in the loop. There is no audio. There is no real time.

Three things make this work:

- A slim `applet_indices.h` enum lets test TUs name applets without pulling in vendor headers that violate ODR.
- A test seam `hem_test::get_applet_impl(HemispheresInstance*, int side)` lives in `applets/Hemispheres.cpp` (the canonical TU that owns the vendor `#define`s) and exposes the active left or right applet pointer.
- The helper module addresses the bus by side, channel, and axis instead of raw bus index, so tests never compute `(bidx - 1) * numFrames` themselves.

### Three-step recipe

For every new applet, do exactly these three things in order. Do not invent a fourth.

1. Read the vendor source. Open `lib/O_C-BankCBlind/software/o_c_REV/HEM_<Applet>.ino`. Find `Start()`, `Controller()`, `OnDataRequest()`, and `OnDataReceive()`. Write down: which CV inputs it consumes, which outputs it drives, which gate inputs trigger behavior, what state it serializes, and the exact bit layout of `OnDataRequest`. Cross-check the bit layout against `Pack(data, PackLocation{offset, width}, value)` calls. The order of `Pack` calls is the bit order. Note any gaps (bits skipped between `Pack` calls). Note any width that does not match the field's natural type.

2. Add a `pack_<applet>` helper to `applet_test_helpers.{h,cpp}` if the applet serializes state. If it serializes nothing (Button, GatedVCA), skip this step. The helper takes named parameters in field order, validates ranges with `REQUIRE` or `assert`, packs into a `uint64_t` matching the vendor bit layout exactly, and explicitly zeroes any documented gap bits. Keep the helper signature stable: future tests will call it.

3. Add a per-applet setup helper plus TEST_CASEs in `test_hemispheres.cpp`. The setup helper calls `setup_applet(kApplet<Name>, LEFT)` and returns the `AppletSetup`. The TEST_CASEs cover, at minimum: Start defaults (what `Controller()` does on frame 1 with no inputs), each documented branch in `Controller()` (one case per branch), and a serialize round-trip via `OnDataRequest` then `OnDataReceive` (if the applet serializes state). Tag every case with `[<applet_tag>]` so `make test-applets ARGS='[<tag>]'` runs the subset.

### Bus contract (do not relearn this)

| Axis | Bus indices | Helper |
|---|---|---|
| Gate inputs | 1..4 (channels A..D) | `set_gate`, `hold_gate` |
| CV inputs | 5..8 (channels A..D) | `set_cv` |
| CV outputs | 13..16 (channels A..D) | `read_cv_at` |
| Gate outputs | 13..16 (channels A..D, same range as CV out) | `read_gate_at` |

Left side owns channels A and B. Right side owns C and D. The helper takes `HemSide` and a per-side channel index (0 or 1), and resolves to the absolute bus index internally. Never index the bus directly in a test.

### Unit conventions

- Pitch CV in hem units. 1V = 1536 (one octave). `volts_to_int(float v)` rounds via `std::lroundf`, not truncation. Negative volts round correctly.
- `HEMISPHERE_MAX_CV` is 9216 (6V), not 7680. Saturation tests must use the correct ceiling.
- Gate high is 6.0f on the float bus. `set_gate` writes one frame; `hold_gate` writes all `numFrames` frames in the slice.
- A "frame" inside `Controller()` is one of the 10 iterations per `step()` call (`numFrames/3` where `numFrames = 32`, so 10 iterations). Tests usually want exactly one `step()` per assertion.

### State injection

Hemispheres has no `parameterChanged` callback. Selector swaps propagate inside `step()` via `maybe_swap()`. To force an applet's internal state into a known configuration without driving inputs across many frames:

1. Build packed state with the relevant `pack_<applet>(...)` helper.
2. Cast the instance: `auto* hi = as_instance(self)`.
3. Get the applet pointer: `auto* applet = get_applet(hi, LEFT)`.
4. Call `applet->OnDataReceive(packed)`.

This is the fast path for tests that need to assert behavior at a specific operating point (Calculate operator selection, AttenuateOffset attenuation level, and similar).

### Determinism

For any applet that reads `random()` (Brancher, Burst, TLNeuron, Cumulus), seed at the start of each TEST_CASE with `seed_hem_rng(<int>)`. Same seed gives the same sequence. Without seeding, tests are flaky.

Statistical tests (probability fairness, distribution shape) need at least 1000 trials and a tolerance margin. Phase 1's Brancher B4 case uses 1000 trials at p=50 with a +/-40 window on expected 500.

### Anti-patterns to avoid

The following mistakes were made during Phase 1 or Phase 2 and caught in review. Do not repeat them.

- Including `hemispheres_shim.h` or `HemispheresFactory.h` from a test TU. This produces 66 duplicate symbols because vendor headers define `hem_MIN/MAX/SUM/DIFF/MEAN` non-inline. Use `applet_indices.h` only.
- Duplicating the `HemSide` enum in `applets/Hemispheres.cpp`. The seam takes `int side`. Only the helper header declares `HemSide`.
- Truncating volts with `(int)(v * 1536.0f)`. Negative values round toward zero, not nearest. Use `std::lroundf`.
- Listing right side as channel B. Right side is channels C and D (per-side channels 0 and 1 still, but absolute channels are 2 and 3).
- Forgetting `using Catch::Approx;` at the top of the test TU. Catch1 puts `Approx` in the `Catch::` namespace.
- Asserting on raw bus floats with `==`. Use `Approx(expected).margin(epsilon)`.
- Adding a TEST_CASE without a tag. The `make test-applets ARGS='[<tag>]'` workflow depends on tags.
- Packing a serialized field with the wrong width. Re-read the vendor `Pack(...)` call before writing the helper. A field declared as `uint8_t` may still be packed at 4 bits.
- Skipping gap bits in `pack_<applet>` when the vendor layout has them (Cumulus bits 11..12). Explicitly write `0` into gap bits and assert them in the round-trip test.

### File touch list per new applet

| File | Change |
|---|---|
| `applet_test_helpers.h` | Add `pack_<applet>(...)` declaration (if applet serializes state). |
| `applet_test_helpers.cpp` | Add `pack_<applet>(...)` body. |
| `test_hemispheres.cpp` | Add per-applet setup helper, TEST_CASEs with `[<applet_tag>]`. |

No other files should change. If you find yourself editing `applets/Hemispheres.cpp`, the `Makefile`, or `applet_indices.h` to add a test, stop and check whether you are extending the harness itself rather than adding a single applet test. Harness extensions need their own plan.

### Verification per applet

After adding the TEST_CASEs:

```bash
make test-applets ARGS='[<applet_tag>]'
```

Then run the full suite:

```bash
make test
```

If you changed a `pack_<applet>` helper that other applets share (none do today, but future applets may share helpers), run every affected tag, not just the new one.

### Hardware gate

The host tests cover logic. They do not cover the ARM build path, DSP behavior, or NT firmware integration. After any harness change or any new applet test merges, run the hardware sanity gate from Task 13 step 3 before considering the work shippable.

---

## Spec coverage check

| Spec section | Plan tasks |
|---|---|
| Phase 2 prep notes: Multi-side fixtures | Task 1 (generic `setup_applet(idx, side)`). Tests still use LEFT default; the helper is ready for Phase 2.5 right-side coverage if needed |
| Phase 2 prep notes: Modulate semantics | Tasks 3 (AttenuateOffset), 5 (Burst), 9 (ClkToGate), 12 (Cumulus) exercise Modulate paths via CV-on-input tests |
| Phase 2 prep notes: Bit-layout re-verification | Every per-applet task starts with a re-verify step before writing the pack helper |
| Phase 2 prep notes: Cumulus 2-bit gap | Task 12 explicitly zeroes bits 11..12 in `pack_cumulus` and asserts the gap bits in CU5 |
| Per-applet OnDataRequest bit layouts (Tier 1) | Tasks 2 (Logic), 3 (AttenuateOffset), 4 (Slew), 5 (Burst) |
| Per-applet OnDataRequest bit layouts (Tier 2) | Tasks 6 (Compare), 7 (GatedVCA), 8 (Button), 9 (ClkToGate), 10 (GateDelay), 11 (TLNeuron), 12 (Cumulus) |
| Adding a new applet (3-step recipe) | Demonstrated in every per-applet task: read vendor, write pack, add TEST_CASEs |
| Final verification commands | Task 13 |
| Hardware re-verification gate | Task 13 step 3 |
