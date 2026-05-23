# Applet Logic Tests (Track A, Phase 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a host-side Catch2 test binary that validates Brancher and Calculate applet logic through the Hemispheres plug-in's NT API, with a reusable helper module designed for Phase 2 expansion to the remaining 11 applets.

**Architecture:** Static-link `applets/Hemispheres.cpp` into a host Catch2 binary (`build/host/test_hemispheres`) via the existing `plugin_loader` weak-symbol pattern. Tests use a side, channel, axis-addressed helper API to drive bus inputs and read bus outputs. Internal applet state is injected by casting `_NT_algorithm*` to `HemispheresInstance*` and calling `OnDataReceive(packed_state)`. RNG is deterministic via the shim's seedable `hem_rng_state` global.

**Tech Stack:** C++14 (host), Catch2 (already vendored at `harness/include/catch.hpp`), GNU Make, clang++.

**Spec reference:** `docs/superpowers/specs/2026-05-17-applet-tests-track-a-design.md`.

---

## File Structure

New files:

- `harness/tests/test_hemispheres.cpp`: Catch2 TEST_CASE entries for smoke, Brancher, Calculate.
- `harness/tests/applet_test_helpers.h`: declarations (side/channel/axis API, applet seam, RNG, packing).
- `harness/tests/applet_test_helpers.cpp`: implementations.

Modified:

- `Makefile`: new `build/host/Hemispheres.host.o`, `build/host/test_hemispheres`, `.PHONY: test-applets`, chained into `test`.

No changes to vendor, shim, plug-in source, or existing harness files.

---

## Conventions used in tasks

Every task takes the worktree from a green state to a green state. After each task: `make arm` still builds, `make test` still passes (after the chain is wired in Task 1), and the new test cases pass.

Commit message format: `feat(test-applets): <task summary>` for new tests, `feat(build): ...` for Makefile changes.

Constants resolved during recon, used throughout the plan:

- Gate "high" threshold in bus floats: `> 0.5f` (shim's `read_gate`).
- Gate "high" output value from `GateOut(ch, true)`: `PULSE_VOLTAGE * ONE_OCTAVE = 6 * 1536 = 9216` hem units, written to bus as `9216 / 1536 = 6.0f`.
- `ONE_OCTAVE = 1536`. `volts_to_int(v) = (int)(v * 1536.0f)`.
- `HEMISPHERE_MAX_CV = 9216`, `HEMISPHERE_MIN_CV = -9216`.
- Hemispheres routing param indices (from `shim/include/hemispheres_shim.h`): `kHemSelLeft=0, kHemSelRight=1, kHemGateInA=2, kHemGateInB=3, kHemGateInC=4, kHemGateInD=5, kHemCvInA=6, kHemCvInB=7, kHemCvInC=8, kHemCvInD=9, kHemCvOutA=10, kHemCvOutAMode=11, kHemCvOutB=12, kHemCvOutBMode=13, kHemCvOutC=14, kHemCvOutCMode=15, kHemCvOutD=16, kHemCvOutDMode=17`.
- Default param values from `_NT_parameter` declarations: gate inputs default to bus 1..4, CV inputs to bus 5..8, outputs to bus 13..16. Output mode default is 1 (replace).
- Hemispheres has no `parameterChanged` callback in its factory. Selector changes propagate inside `step()` via `maybe_swap()`. Tests write `alg->v[kHemSelLeft]` and the next `step()` performs the swap.
- `Controller()` runs `numFrames / 3` times per `step()` call. With `numFramesBy4 = 8` (32 frames), that's 10 Controller calls per step.
- Gate-edge detection scans all 32 frames of the input bus per step; any rising edge within the buffer counts as one `Clock()` true event for that step.

---

## Task 1: Build pipeline and smoke test

**Files:**
- Create: `harness/tests/test_hemispheres.cpp`
- Modify: `Makefile`

- [ ] **Step 1: Write the failing test**

Create `harness/tests/test_hemispheres.cpp`:

```cpp
#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include "../../shim/include/hemispheres_shim.h"
#include "../../shim/include/HemispheresFactory.h"
#include <distingnt/api.h>
#include <cstring>

using hem_shim::HemispheresInstance;

TEST_CASE("hemispheres factory loads, steps, draws without crash", "[smoke]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);

    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->factory != nullptr);
    REQUIRE(loaded->algorithm != nullptr);

    auto* alg = loaded->algorithm;
    const_cast<int16_t*>(alg->v)[0] = (int16_t)kAppletBrancher;
    const_cast<int16_t*>(alg->v)[1] = (int16_t)kAppletCalculate;

    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    std::memset(bus, 0, sizeof(float) * 64 * 32);

    for (int i = 0; i < 10; ++i) {
        loaded->factory->step(alg, bus, 8);
    }

    REQUIRE(loaded->factory->draw(alg) == true);

    for (int b = 1; b <= 16; ++b) {
        const float* slice = bus + (b - 1) * 32;
        for (int f = 0; f < 32; ++f) {
            REQUIRE(std::isfinite(slice[f]));
        }
    }
}
```

- [ ] **Step 2: Run test to verify it fails to build**

Run:

```bash
make test-applets
```

Expected: build error (`No rule to make target 'test-applets'` or missing `build/host/test_hemispheres` target).

- [ ] **Step 3: Add Makefile targets**

Edit `Makefile`. In the `.PHONY` declaration line (line ~2), add `test-applets`:

```
.PHONY: help vendor host arm test test-runtime clean deploy deploy-sysex test-applets
```

After the existing `build/arm/Hemispheres.o` rule (around line 121), add:

```
build/host/Hemispheres.host.o: applets/Hemispheres.cpp $(SHIM_DEPS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -c -o $@ $<

build/host/test_hemispheres: harness/tests/test_hemispheres.cpp build/host/Hemispheres.host.o $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^

test-applets: build/host/test_hemispheres
	./build/host/test_hemispheres
```

Update the `test:` rule to depend on `test-applets`. Find the existing `test:` line:

```
test: host
	python3 harness/scripts/run_scenario.py tests/scenarios/gainCustomUI/zero_signal.yaml
```

Change to:

```
test: host test-applets
	python3 harness/scripts/run_scenario.py tests/scenarios/gainCustomUI/zero_signal.yaml
```

- [ ] **Step 4: Run test to verify it passes**

Run:

```bash
make test-applets
```

Expected: build succeeds; output ends with `All tests passed (X assertions in 1 test case)`.

Run also:

```bash
make arm
make test
```

Expected: ARM build clean. `make test` runs `test-applets` and then the YAML scenario; both pass.

- [ ] **Step 5: Commit**

```bash
git add Makefile harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(build): host build of Hemispheres + smoke test binary"
```

---

## Task 2: Helper module foundation

**Files:**
- Create: `harness/tests/applet_test_helpers.h`
- Create: `harness/tests/applet_test_helpers.cpp`
- Modify: `Makefile` (link the helper TU into `test_hemispheres`)
- Modify: `harness/tests/test_hemispheres.cpp` (port smoke to use helpers)

- [ ] **Step 1: Write the helper interface as a failing test extension**

Modify `harness/tests/test_hemispheres.cpp`. Replace the existing TEST_CASE body to exercise the helper API:

```cpp
#include "catch.hpp"
#include "nt_runtime.h"
#include "plugin_loader.h"
#include "applet_test_helpers.h"
#include <cstring>

using namespace hem_test;

TEST_CASE("hemispheres factory loads, steps, draws without crash", "[smoke]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);

    auto* loaded = nt::load_plugin();
    REQUIRE(loaded != nullptr);

    auto* alg = loaded->algorithm;
    select_applet(alg, LEFT,  kAppletBrancher);
    select_applet(alg, RIGHT, kAppletCalculate);

    float* bus = nt::bus_frames_base();
    REQUIRE(bus != nullptr);
    std::memset(bus, 0, sizeof(float) * 64 * 32);

    int advanced = step_n_frames(loaded, alg, bus, 320);
    REQUIRE(advanced == 320);

    REQUIRE(loaded->factory->draw(alg) == true);

    for (int b = 1; b <= 16; ++b) {
        const float* slice = bus + (b - 1) * 32;
        for (int f = 0; f < 32; ++f) {
            REQUIRE(std::isfinite(slice[f]));
        }
    }
}
```

- [ ] **Step 2: Run test to verify it fails to build**

Run:

```bash
make test-applets
```

Expected: build error (`fatal error: 'applet_test_helpers.h' file not found`).

- [ ] **Step 3: Create the helper header**

Create `harness/tests/applet_test_helpers.h`:

```cpp
#pragma once
#include <cstdint>
#include <distingnt/api.h>
#include "plugin_loader.h"
#include "../../shim/include/HemispheresFactory.h"
#include "../../shim/include/hemispheres_shim.h"

namespace hem_test {

enum HemSide { LEFT = 0, RIGHT = 1 };
enum HemAxis { GATE_IN, CV_IN, OUT };

// Returns the 1-based NT bus index for the given side/channel/axis under default routing.
int bus_index(HemSide side, int channel, HemAxis axis);

// Writes a single-sample gate pulse (6.0V) at frame_offset on the input gate bus
// for (side, channel). Other frames in that bus stay zero unless the caller wrote them.
void set_gate(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4);

// Writes a constant CV value (volts) across all frames of the CV input bus
// for (side, channel).
void set_cv(float* bus, HemSide side, int channel, float volts, int numFramesBy4);

// Reads the output bus for (side, channel) at the given frame and returns true
// if the value is above the gate threshold (>0.5f).
bool read_gate_at(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4);

// Reads the output bus for (side, channel) at the given frame in volts.
float read_cv_at(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4);

// Sets the selector parameter for the given side. The next step() will trigger
// the applet swap inside the shim. No external parameterChanged() call needed.
void select_applet(_NT_algorithm* alg, HemSide side, AppletIndex idx);

// Typed accessor for the side's applet instance.
HemisphereApplet* get_applet(hem_shim::HemispheresInstance* hi, HemSide side);

// Typed cast wrapper.
hem_shim::HemispheresInstance* as_instance(_NT_algorithm* alg);

// Issues ceil(n_samples / 32) step() calls. Returns total frames advanced
// (a multiple of 32).
int step_n_frames(nt::LoadedPlugin* loaded, _NT_algorithm* alg, float* bus, int n_samples);

// Vendor int unit conversions. ONE_OCTAVE = 1536 hem units per volt.
int   volts_to_int(float v);
float int_to_volts(int hem_units);

// Writes the shim's hem_rng_state global. Tests call this before any RNG-touching scenario.
void seed_hem_rng(uint32_t seed);

}  // namespace hem_test
```

- [ ] **Step 4: Create the helper implementation**

Create `harness/tests/applet_test_helpers.cpp`:

```cpp
#include "applet_test_helpers.h"
#include "nt_runtime.h"
#include <cstring>

extern uint32_t hem_rng_state;  // defined in shim/src/globals.cpp

namespace hem_test {

int bus_index(HemSide side, int channel, HemAxis axis) {
    // Channels A,B (0,1) are left side; C,D (2,3) are right side.
    int slot = (side == LEFT) ? channel : (channel + 2);
    switch (axis) {
        case GATE_IN: return 1  + slot;  // 1..4
        case CV_IN:   return 5  + slot;  // 5..8
        case OUT:     return 13 + slot;  // 13..16
    }
    return 0;
}

void set_gate(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4) {
    int bidx = bus_index(side, channel, GATE_IN);
    int numFrames = numFramesBy4 * 4;
    float* slice = bus + (bidx - 1) * numFrames;
    slice[frame_offset] = 6.0f;  // above 0.5f gate threshold
}

void set_cv(float* bus, HemSide side, int channel, float volts, int numFramesBy4) {
    int bidx = bus_index(side, channel, CV_IN);
    int numFrames = numFramesBy4 * 4;
    float* slice = bus + (bidx - 1) * numFrames;
    for (int i = 0; i < numFrames; ++i) slice[i] = volts;
}

bool read_gate_at(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4) {
    int bidx = bus_index(side, channel, OUT);
    int numFrames = numFramesBy4 * 4;
    return bus[(bidx - 1) * numFrames + frame_offset] > 0.5f;
}

float read_cv_at(float* bus, HemSide side, int channel, int frame_offset, int numFramesBy4) {
    int bidx = bus_index(side, channel, OUT);
    int numFrames = numFramesBy4 * 4;
    return bus[(bidx - 1) * numFrames + frame_offset];
}

void select_applet(_NT_algorithm* alg, HemSide side, AppletIndex idx) {
    int param = (side == LEFT) ? kHemSelLeft : kHemSelRight;
    const_cast<int16_t*>(alg->v)[param] = (int16_t)idx;
}

HemisphereApplet* get_applet(hem_shim::HemispheresInstance* hi, HemSide side) {
    return (side == LEFT) ? hi->left : hi->right;
}

hem_shim::HemispheresInstance* as_instance(_NT_algorithm* alg) {
    return static_cast<hem_shim::HemispheresInstance*>(alg);
}

int step_n_frames(nt::LoadedPlugin* loaded, _NT_algorithm* alg, float* bus, int n_samples) {
    const int framesPerStep = 32;
    int steps = (n_samples + framesPerStep - 1) / framesPerStep;
    for (int i = 0; i < steps; ++i) {
        loaded->factory->step(alg, bus, 8);
    }
    return steps * framesPerStep;
}

int volts_to_int(float v) {
    return (int)(v * 1536.0f);
}

float int_to_volts(int hem_units) {
    return (float)hem_units / 1536.0f;
}

void seed_hem_rng(uint32_t seed) {
    hem_rng_state = seed;
}

}  // namespace hem_test
```

- [ ] **Step 5: Add the helper TU to the test binary**

Edit `Makefile`. Update the `build/host/test_hemispheres` rule to link the helper:

```
build/host/test_hemispheres: harness/tests/test_hemispheres.cpp harness/tests/applet_test_helpers.cpp build/host/Hemispheres.host.o $(HARNESS_SRCS)
	mkdir -p build/host
	$(HOST_CXX) $(HOST_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -o $@ $^
```

- [ ] **Step 6: Run test to verify it passes**

```bash
make test-applets
```

Expected: `All tests passed (X assertions in 1 test case)`.

- [ ] **Step 7: Commit**

```bash
git add Makefile harness/tests/test_hemispheres.cpp harness/tests/applet_test_helpers.h harness/tests/applet_test_helpers.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): helper module with side/channel/axis API"
```

---

## Task 3: Calculate C1 (Start defaults) and C13 (serialise round-trip)

**Files:**
- Modify: `harness/tests/applet_test_helpers.h` (add `pack_calculate`)
- Modify: `harness/tests/applet_test_helpers.cpp` (implement)
- Modify: `harness/tests/test_hemispheres.cpp` (add tests)

- [ ] **Step 1: Write the failing tests**

Append to `harness/tests/test_hemispheres.cpp`:

```cpp
TEST_CASE("calculate C1: Start defaults are MIN, MAX", "[calculate]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletCalculate);

    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);  // triggers swap + Start

    auto* hi = as_instance(alg);
    uint64_t packed = get_applet(hi, LEFT)->OnDataRequest();
    int op0 = (int)(packed & 0xFF);
    int op1 = (int)((packed >> 8) & 0xFF);
    REQUIRE(op0 == 0);  // MIN_FN
    REQUIRE(op1 == 1);  // MAX_FN
}

TEST_CASE("calculate C13: serialise round-trip", "[calculate]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletCalculate);

    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);

    auto* hi = as_instance(alg);
    get_applet(hi, LEFT)->OnDataReceive(pack_calculate(5, 7));
    uint64_t packed = get_applet(hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0xFF)         == 5);
    REQUIRE(((packed >> 8) & 0xFF)  == 7);
}
```

- [ ] **Step 2: Run tests to verify they fail to build**

```bash
make test-applets
```

Expected: build error `use of undeclared identifier 'pack_calculate'`.

- [ ] **Step 3: Add pack_calculate declaration**

Edit `harness/tests/applet_test_helpers.h`. After the `seed_hem_rng` declaration, add:

```cpp
// Mirrors Calculate::OnDataRequest packing: bits [0,8] = op_left, [8,8] = op_right.
uint64_t pack_calculate(int op_left, int op_right);
```

- [ ] **Step 4: Add pack_calculate implementation**

Edit `harness/tests/applet_test_helpers.cpp`. After `seed_hem_rng`, add:

```cpp
uint64_t pack_calculate(int op_left, int op_right) {
    return ((uint64_t)(op_left  & 0xFF))
         | ((uint64_t)(op_right & 0xFF) << 8);
}
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
./build/host/test_hemispheres "[calculate]"
```

Expected: `All tests passed (X assertions in 2 test cases)`.

- [ ] **Step 6: Commit**

```bash
git add harness/tests/applet_test_helpers.h harness/tests/applet_test_helpers.cpp harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): calculate C1 defaults + C13 serialise round-trip"
```

---

## Task 4: Calculate C2-C5 (MIN, MAX, SUM clamp high, SUM clamp low)

**Files:**
- Modify: `harness/tests/test_hemispheres.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `harness/tests/test_hemispheres.cpp`:

```cpp
namespace {
void calculate_set_op(_NT_algorithm* alg, int op_left, int op_right) {
    auto* hi = as_instance(alg);
    get_applet(hi, LEFT)->OnDataReceive(pack_calculate(op_left, op_right));
}
}

TEST_CASE("calculate C2: MIN selects lesser of In(0), In(1)", "[calculate]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletCalculate);

    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);
    calculate_set_op(alg, 0, 0);  // MIN both channels

    std::memset(bus, 0, sizeof(float) * 64 * 32);
    set_cv(bus, LEFT, 0, 2.0f, 8);
    set_cv(bus, LEFT, 1, 4.0f, 8);
    step_n_frames(loaded, alg, bus, 32);

    REQUIRE(read_cv_at(bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.01f));
    REQUIRE(read_cv_at(bus, LEFT, 1, 0, 8) == Approx(2.0f).margin(0.01f));
}

TEST_CASE("calculate C3: MAX selects greater of In(0), In(1)", "[calculate]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletCalculate);

    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);
    calculate_set_op(alg, 1, 1);  // MAX both channels

    std::memset(bus, 0, sizeof(float) * 64 * 32);
    set_cv(bus, LEFT, 0, 2.0f, 8);
    set_cv(bus, LEFT, 1, 4.0f, 8);
    step_n_frames(loaded, alg, bus, 32);

    REQUIRE(read_cv_at(bus, LEFT, 0, 0, 8) == Approx(4.0f).margin(0.01f));
    REQUIRE(read_cv_at(bus, LEFT, 1, 0, 8) == Approx(4.0f).margin(0.01f));
}

TEST_CASE("calculate C4: SUM clamps at HEMISPHERE_MAX_CV", "[calculate]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletCalculate);

    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);
    calculate_set_op(alg, 2, 2);  // SUM both

    std::memset(bus, 0, sizeof(float) * 64 * 32);
    set_cv(bus, LEFT, 0, 6.0f, 8);     // = HEMISPHERE_MAX_CV
    set_cv(bus, LEFT, 1, 0.5f, 8);     // would push past max
    step_n_frames(loaded, alg, bus, 32);

    REQUIRE(read_cv_at(bus, LEFT, 0, 0, 8) == Approx(6.0f).margin(0.01f));
}

TEST_CASE("calculate C5: SUM clamps at HEMISPHERE_MIN_CV", "[calculate]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletCalculate);

    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);
    calculate_set_op(alg, 2, 2);

    std::memset(bus, 0, sizeof(float) * 64 * 32);
    set_cv(bus, LEFT, 0, -6.0f, 8);    // = HEMISPHERE_MIN_CV
    set_cv(bus, LEFT, 1, -0.5f, 8);
    step_n_frames(loaded, alg, bus, 32);

    REQUIRE(read_cv_at(bus, LEFT, 0, 0, 8) == Approx(-6.0f).margin(0.01f));
}
```

- [ ] **Step 2: Run tests to verify they pass**

```bash
./build/host/test_hemispheres "[calculate]"
```

Expected: 6 test cases pass (C1, C2, C3, C4, C5, C13).

- [ ] **Step 3: Commit**

```bash
git add harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): calculate C2-C5 MIN/MAX/SUM clamps"
```

---

## Task 5: Calculate C6-C8 (DIFF, MEAN, asymmetric quirk)

**Files:**
- Modify: `harness/tests/test_hemispheres.cpp`

- [ ] **Step 1: Write the failing tests**

Append:

```cpp
TEST_CASE("calculate C6: DIFF returns absolute difference", "[calculate]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletCalculate);
    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);
    calculate_set_op(alg, 3, 3);  // DIFF both

    std::memset(bus, 0, sizeof(float) * 64 * 32);
    set_cv(bus, LEFT, 0, 1.0f, 8);
    set_cv(bus, LEFT, 1, 3.0f, 8);
    step_n_frames(loaded, alg, bus, 32);
    REQUIRE(read_cv_at(bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.01f));

    // Swap inputs: DIFF should still be 2.0
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    set_cv(bus, LEFT, 0, 3.0f, 8);
    set_cv(bus, LEFT, 1, 1.0f, 8);
    step_n_frames(loaded, alg, bus, 32);
    REQUIRE(read_cv_at(bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.01f));
}

TEST_CASE("calculate C7: MEAN returns (a+b)/2", "[calculate]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletCalculate);
    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);
    calculate_set_op(alg, 4, 4);  // MEAN both

    std::memset(bus, 0, sizeof(float) * 64 * 32);
    set_cv(bus, LEFT, 0, 1.0f, 8);
    set_cv(bus, LEFT, 1, 3.0f, 8);
    step_n_frames(loaded, alg, bus, 32);
    REQUIRE(read_cv_at(bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.01f));
}

TEST_CASE("calculate C8: both channels read both inputs (asymmetric quirk)", "[calculate]") {
    // Vendor: result = calc_fn[op[ch]](In(0), In(1)).  In(0) and In(1) are
    // shared across channels, not per-channel. So op[0]=MIN + op[1]=MAX with
    // In(0)=1 and In(1)=3 yields Out(0)=1 and Out(1)=3.
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletCalculate);
    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);
    calculate_set_op(alg, 0, 1);  // op[0]=MIN, op[1]=MAX

    std::memset(bus, 0, sizeof(float) * 64 * 32);
    set_cv(bus, LEFT, 0, 1.0f, 8);
    set_cv(bus, LEFT, 1, 3.0f, 8);
    step_n_frames(loaded, alg, bus, 32);
    REQUIRE(read_cv_at(bus, LEFT, 0, 0, 8) == Approx(1.0f).margin(0.01f));
    REQUIRE(read_cv_at(bus, LEFT, 1, 0, 8) == Approx(3.0f).margin(0.01f));
}
```

- [ ] **Step 2: Run tests to verify they pass**

```bash
./build/host/test_hemispheres "[calculate]"
```

Expected: 9 test cases pass.

- [ ] **Step 3: Commit**

```bash
git add harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): calculate C6-C8 DIFF/MEAN/asymmetric"
```

---

## Task 6: Calculate C9-C10 (S&H pre and post clock)

**Files:**
- Modify: `harness/tests/test_hemispheres.cpp`

- [ ] **Step 1: Write the failing tests**

Append:

```cpp
TEST_CASE("calculate C9: S&H output stays at zero before clock", "[calculate]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletCalculate);
    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);
    calculate_set_op(alg, 5, 5);  // S&H both

    std::memset(bus, 0, sizeof(float) * 64 * 32);
    set_cv(bus, LEFT, 0, 2.0f, 8);  // input CV present
    set_cv(bus, LEFT, 1, 3.0f, 8);
    // No gate input written.
    step_n_frames(loaded, alg, bus, 32);

    REQUIRE(read_cv_at(bus, LEFT, 0, 0, 8) == Approx(0.0f).margin(0.01f));
    REQUIRE(read_cv_at(bus, LEFT, 1, 0, 8) == Approx(0.0f).margin(0.01f));
}

TEST_CASE("calculate C10: S&H captures input on clock edge", "[calculate]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletCalculate);
    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);
    calculate_set_op(alg, 5, 5);  // S&H both

    // First step: gate edge + held CV.
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    set_cv(bus, LEFT, 0, 2.0f, 8);
    set_gate(bus, LEFT, 0, 0, 8);  // rising edge at frame 0
    step_n_frames(loaded, alg, bus, 32);

    // Run multiple subsequent steps to clear EndOfADCLag latency. ADC lag is
    // implementation-defined; stepping ~500 samples is safely past it.
    for (int i = 0; i < 16; ++i) {
        std::memset(bus, 0, sizeof(float) * 64 * 32);
        set_cv(bus, LEFT, 0, 2.0f, 8);  // input CV continues to be 2V
        step_n_frames(loaded, alg, bus, 32);
    }

    REQUIRE(read_cv_at(bus, LEFT, 0, 0, 8) == Approx(2.0f).margin(0.1f));
}
```

- [ ] **Step 2: Run tests to verify they pass**

```bash
./build/host/test_hemispheres "[calculate]"
```

Expected: 11 test cases pass.

- [ ] **Step 3: Commit**

```bash
git add harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): calculate C9-C10 S&H pre and post clock"
```

---

## Task 7: Calculate C11-C12 (Rnd unipolar range, Rnd clocked latch)

**Files:**
- Modify: `harness/tests/test_hemispheres.cpp`

- [ ] **Step 1: Write the failing tests**

Append:

```cpp
TEST_CASE("calculate C11: Rnd+ outputs in [0, HEMISPHERE_MAX_CV)", "[calculate]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletCalculate);
    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);
    calculate_set_op(alg, 6, 6);  // Rnd+ both
    seed_hem_rng(0xDEADBEEF);

    bool saw_nonzero = false;
    for (int i = 0; i < 100; ++i) {
        std::memset(bus, 0, sizeof(float) * 64 * 32);
        step_n_frames(loaded, alg, bus, 32);
        float v = read_cv_at(bus, LEFT, 0, 0, 8);
        REQUIRE(v >= 0.0f);
        REQUIRE(v <  6.001f);  // HEMISPHERE_MAX_CV = 6.0V plus tiny margin
        if (v > 0.01f) saw_nonzero = true;
    }
    REQUIRE(saw_nonzero);  // at least one nonzero out of 100 random rolls
}

TEST_CASE("calculate C12: Rnd+ latches to clocked after first Clock(0)", "[calculate]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletCalculate);
    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);
    calculate_set_op(alg, 6, 6);  // Rnd+
    seed_hem_rng(0xCAFEBABE);

    // Step 1: rising edge on Clock(0). Latches to clocked.
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    set_gate(bus, LEFT, 0, 0, 8);
    step_n_frames(loaded, alg, bus, 32);
    float v_after_clock = read_cv_at(bus, LEFT, 0, 0, 8);

    // Subsequent unclocked steps: output should not change.
    for (int i = 0; i < 5; ++i) {
        std::memset(bus, 0, sizeof(float) * 64 * 32);
        // no gate edge
        step_n_frames(loaded, alg, bus, 32);
        REQUIRE(read_cv_at(bus, LEFT, 0, 0, 8) == Approx(v_after_clock).margin(0.001f));
    }
}
```

- [ ] **Step 2: Run tests to verify they pass**

```bash
./build/host/test_hemispheres "[calculate]"
```

Expected: 13 test cases pass.

- [ ] **Step 3: Commit**

```bash
git add harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): calculate C11-C12 Rnd range + clocked latch"
```

---

## Task 8: Brancher B1 (Start defaults) + B9 (serialise round-trip)

**Files:**
- Modify: `harness/tests/applet_test_helpers.h` (add `pack_brancher`)
- Modify: `harness/tests/applet_test_helpers.cpp` (implement)
- Modify: `harness/tests/test_hemispheres.cpp` (add tests)

- [ ] **Step 1: Write the failing tests**

Append to `harness/tests/test_hemispheres.cpp`:

```cpp
TEST_CASE("brancher B1: Start sets p = 50", "[brancher]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletBrancher);
    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);

    auto* hi = as_instance(alg);
    uint64_t packed = get_applet(hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0x7F) == 50);
}

TEST_CASE("brancher B9: serialise round-trip preserves p", "[brancher]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletBrancher);
    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);

    auto* hi = as_instance(alg);
    get_applet(hi, LEFT)->OnDataReceive(pack_brancher(73));
    uint64_t packed = get_applet(hi, LEFT)->OnDataRequest();
    REQUIRE((packed & 0x7F) == 73);
}
```

- [ ] **Step 2: Run tests to verify they fail to build**

```bash
make test-applets
```

Expected: `use of undeclared identifier 'pack_brancher'`.

- [ ] **Step 3: Add pack_brancher declaration**

Edit `harness/tests/applet_test_helpers.h`. After `pack_calculate`:

```cpp
// Mirrors Brancher::OnDataRequest packing: bits [0,7] = p in 0..100. Note that
// Brancher's `choice` field is not serialised by the vendor.
uint64_t pack_brancher(int p);
```

- [ ] **Step 4: Add pack_brancher implementation**

Edit `harness/tests/applet_test_helpers.cpp`. After `pack_calculate`:

```cpp
uint64_t pack_brancher(int p) {
    return (uint64_t)(p & 0x7F);
}
```

- [ ] **Step 5: Run tests to verify they pass**

```bash
./build/host/test_hemispheres "[brancher]"
```

Expected: 2 test cases pass.

- [ ] **Step 6: Commit**

```bash
git add harness/tests/applet_test_helpers.h harness/tests/applet_test_helpers.cpp harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): brancher B1 defaults + B9 serialise round-trip"
```

---

## Task 9: Brancher B2 (p=100 deterministic) + B3 (p=0 deterministic)

**Files:**
- Modify: `harness/tests/test_hemispheres.cpp`

- [ ] **Step 1: Write the failing tests**

Append:

```cpp
TEST_CASE("brancher B2: p=100 always routes gate to output 0", "[brancher]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletBrancher);
    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);

    auto* hi = as_instance(alg);
    get_applet(hi, LEFT)->OnDataReceive(pack_brancher(100));
    seed_hem_rng(0xDEADBEEF);

    // Physical gate: rising edge + sustained high in the input bus.
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    // Sustained high across all 32 frames so Gate(0) reads true.
    int gate_bus = bus_index(LEFT, 0, GATE_IN);
    for (int f = 0; f < 32; ++f) bus[(gate_bus - 1) * 32 + f] = 6.0f;
    step_n_frames(loaded, alg, bus, 32);

    REQUIRE(read_gate_at(bus, LEFT, 0, 0, 8) == true);   // output 0 high
    REQUIRE(read_gate_at(bus, LEFT, 1, 0, 8) == false);  // output 1 low
}

TEST_CASE("brancher B3: p=0 always routes gate to output 1", "[brancher]") {
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletBrancher);
    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);

    auto* hi = as_instance(alg);
    get_applet(hi, LEFT)->OnDataReceive(pack_brancher(0));
    seed_hem_rng(0xDEADBEEF);

    std::memset(bus, 0, sizeof(float) * 64 * 32);
    int gate_bus = bus_index(LEFT, 0, GATE_IN);
    for (int f = 0; f < 32; ++f) bus[(gate_bus - 1) * 32 + f] = 6.0f;
    step_n_frames(loaded, alg, bus, 32);

    REQUIRE(read_gate_at(bus, LEFT, 0, 0, 8) == false);
    REQUIRE(read_gate_at(bus, LEFT, 1, 0, 8) == true);
}
```

- [ ] **Step 2: Run tests to verify they pass**

```bash
./build/host/test_hemispheres "[brancher]"
```

Expected: 4 test cases pass.

- [ ] **Step 3: Commit**

```bash
git add harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): brancher B2/B3 deterministic routing at p=100, p=0"
```

---

## Task 10: Brancher B5 (logical clock pulse)

**Files:**
- Modify: `harness/tests/test_hemispheres.cpp`

- [ ] **Step 1: Write the failing test**

Append:

```cpp
TEST_CASE("brancher B5: logical clock (no physical gate) emits ClockOut pulse", "[brancher]") {
    // Vendor: if (Clock(0)) { clocked = !Gate(0); if (clocked) ClockOut(choice); }
    // Pulse the gate input for one frame only so a rising edge fires but the
    // gate is not held high across the buffer. Gate(0) returns false because
    // last_high is reset before the next step. ClockOut emits a brief pulse
    // (HEMISPHERE_CLOCK_TICKS = 175 ticks).
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletBrancher);
    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);

    auto* hi = as_instance(alg);
    get_applet(hi, LEFT)->OnDataReceive(pack_brancher(100));
    seed_hem_rng(0xDEADBEEF);

    // Single-sample pulse: rising edge fires, but high flag clears before
    // the next read_gate scan.
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    set_gate(bus, LEFT, 0, 0, 8);  // pulse at frame 0 only
    step_n_frames(loaded, alg, bus, 32);

    // After the step, output 0 should have received the ClockOut pulse (high)
    // and output 1 should be low.
    REQUIRE(read_gate_at(bus, LEFT, 0, 0, 8) == true);
    REQUIRE(read_gate_at(bus, LEFT, 1, 0, 8) == false);
}
```

- [ ] **Step 2: Run test to verify it passes**

```bash
./build/host/test_hemispheres "[brancher]"
```

Expected: 5 test cases pass.

- [ ] **Step 3: Commit**

```bash
git add harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): brancher B5 logical clock pulse via ClockOut"
```

---

## Task 11: Brancher B6 (flip-flop mode toggles on Clock(1))

**Files:**
- Modify: `harness/tests/test_hemispheres.cpp`

- [ ] **Step 1: Write the failing test**

Append:

```cpp
TEST_CASE("brancher B6: Clock(1) toggles flip-flop choice", "[brancher]") {
    // Vendor: Clock(1) enters flipflopmode and re-rolls choice. Output stays
    // high (GateOut(choice, flipflopmode)) without further Clock(0). The next
    // Clock(1) re-rolls choice again. With p=100, every roll selects choice=0.
    // So we need p=0 (or a seed that produces a flip) to observe a toggle.
    // We seed RNG and use p=50 + a 1000-step Clock(1) loop to ensure we
    // observe both states. Simpler: set p=100 first, observe output 0; set
    // p=0, fire Clock(1), observe output 1; set p=100, fire Clock(1), observe
    // output 0 again.
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletBrancher);
    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);

    auto* hi = as_instance(alg);

    // Fire Clock(1) with p=100. choice should roll to 0. Output 0 high.
    get_applet(hi, LEFT)->OnDataReceive(pack_brancher(100));
    seed_hem_rng(0xDEADBEEF);
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    set_gate(bus, LEFT, 1, 0, 8);  // Clock(1) edge
    step_n_frames(loaded, alg, bus, 32);
    REQUIRE(read_gate_at(bus, LEFT, 0, 0, 8) == true);

    // Fire Clock(1) with p=0. choice should roll to 1. Output 1 high.
    get_applet(hi, LEFT)->OnDataReceive(pack_brancher(0));
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    set_gate(bus, LEFT, 1, 0, 8);
    step_n_frames(loaded, alg, bus, 32);
    REQUIRE(read_gate_at(bus, LEFT, 1, 0, 8) == true);
    REQUIRE(read_gate_at(bus, LEFT, 0, 0, 8) == false);
}
```

- [ ] **Step 2: Run test to verify it passes**

```bash
./build/host/test_hemispheres "[brancher]"
```

Expected: 6 test cases pass.

- [ ] **Step 3: Commit**

```bash
git add harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): brancher B6 flip-flop toggles on Clock(1)"
```

---

## Task 12: Brancher B7 (OnButtonPress flips choice)

**Files:**
- Modify: `harness/tests/test_hemispheres.cpp`

- [ ] **Step 1: Write the failing test**

Append:

```cpp
TEST_CASE("brancher B7: OnButtonPress flips choice before next gate", "[brancher]") {
    // Vendor: OnButtonPress sets choice = 1 - choice. So with p=100 (choice
    // would roll to 0 on the next gate), pressing the button BEFORE the gate
    // sets choice=1. We have to be careful about ordering: Brancher rolls
    // choice INSIDE the Clock(0) branch. OnButtonPress only affects the
    // current choice; if a clock then re-rolls, the press is overwritten.
    //
    // To assert the press effect we need a scenario where Controller runs
    // WITHOUT a Clock(0) edge between the press and the output read. The
    // physical-gate path takes !clocked || flipflopmode -> GateOut(choice).
    // With no Clock(0) edge in the current step, that branch still runs
    // because clocked is reset by the prior step. We achieve "press, then
    // sustained gate without rising edge" by sustaining the gate across two
    // steps: step 1 raises the gate (so Clock(0) fires; choice rolls to 0),
    // step 2 keeps the gate high (no new rising edge; GateOut emits current
    // choice). Between steps we press the button.
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletBrancher);
    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);

    auto* hi = as_instance(alg);
    get_applet(hi, LEFT)->OnDataReceive(pack_brancher(100));
    seed_hem_rng(0xDEADBEEF);

    // Step 1: raise gate. Clock(0) fires, choice rolls to 0, output 0 high.
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    int gate_bus = bus_index(LEFT, 0, GATE_IN);
    for (int f = 0; f < 32; ++f) bus[(gate_bus - 1) * 32 + f] = 6.0f;
    step_n_frames(loaded, alg, bus, 32);
    REQUIRE(read_gate_at(bus, LEFT, 0, 0, 8) == true);

    // Press the button.
    get_applet(hi, LEFT)->OnButtonPress();

    // Step 2: keep gate high (no rising edge). choice was flipped to 1 by the
    // button press, so GateOut writes output 1 high, output 0 low.
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    for (int f = 0; f < 32; ++f) bus[(gate_bus - 1) * 32 + f] = 6.0f;
    step_n_frames(loaded, alg, bus, 32);
    REQUIRE(read_gate_at(bus, LEFT, 1, 0, 8) == true);
    REQUIRE(read_gate_at(bus, LEFT, 0, 0, 8) == false);
}
```

- [ ] **Step 2: Run test to verify it passes**

```bash
./build/host/test_hemispheres "[brancher]"
```

Expected: 7 test cases pass.

- [ ] **Step 3: Commit**

```bash
git add harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): brancher B7 OnButtonPress flips choice"
```

---

## Task 13: Brancher B8 (encoder clamps p at 0 and 100)

**Files:**
- Modify: `harness/tests/test_hemispheres.cpp`

- [ ] **Step 1: Write the failing test**

Append:

```cpp
TEST_CASE("brancher B8: OnEncoderMove clamps p to [0, 100]", "[brancher]") {
    // Vendor: OnEncoderMove(direction) { p = constrain(p + direction, 0, 100); }
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletBrancher);
    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);
    auto* hi = as_instance(alg);

    // Start at p=50. Push above 100.
    for (int i = 0; i < 30; ++i) get_applet(hi, LEFT)->OnEncoderMove(+5);
    REQUIRE((get_applet(hi, LEFT)->OnDataRequest() & 0x7F) == 100);

    // Push below 0.
    for (int i = 0; i < 30; ++i) get_applet(hi, LEFT)->OnEncoderMove(-5);
    REQUIRE((get_applet(hi, LEFT)->OnDataRequest() & 0x7F) == 0);
}
```

- [ ] **Step 2: Run test to verify it passes**

```bash
./build/host/test_hemispheres "[brancher]"
```

Expected: 8 test cases pass.

- [ ] **Step 3: Commit**

```bash
git add harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): brancher B8 encoder clamps p to [0, 100]"
```

---

## Task 14: Brancher B4 (statistical fairness at p=50)

**Files:**
- Modify: `harness/tests/test_hemispheres.cpp`

- [ ] **Step 1: Write the failing test**

Append:

```cpp
TEST_CASE("brancher B4: p=50 yields ~50/50 routing over 1000 clocks", "[brancher]") {
    // Statistical assertion. Seeded RNG (xorshift32) gives reproducible output;
    // 1000 rolls at p=50 should fall in [460, 540] for either output, well
    // within statistical tolerance (~3 standard deviations).
    nt::reset_runtime();
    nt::set_bus_frame_count(32);
    auto* loaded = nt::load_plugin();
    auto* alg    = loaded->algorithm;
    select_applet(alg, LEFT, kAppletBrancher);
    float* bus = nt::bus_frames_base();
    std::memset(bus, 0, sizeof(float) * 64 * 32);
    step_n_frames(loaded, alg, bus, 32);

    auto* hi = as_instance(alg);
    get_applet(hi, LEFT)->OnDataReceive(pack_brancher(50));
    seed_hem_rng(0xDEADBEEF);

    int count_0 = 0, count_1 = 0;
    int gate_bus = bus_index(LEFT, 0, GATE_IN);
    for (int trial = 0; trial < 1000; ++trial) {
        // Sustained high gate across the buffer. Each step generates exactly
        // one Clock(0) rising edge (the prev_high tracking ensures one edge
        // per buffer transition).
        std::memset(bus, 0, sizeof(float) * 64 * 32);
        for (int f = 0; f < 32; ++f) bus[(gate_bus - 1) * 32 + f] = 6.0f;
        step_n_frames(loaded, alg, bus, 32);
        bool out0 = read_gate_at(bus, LEFT, 0, 0, 8);
        bool out1 = read_gate_at(bus, LEFT, 1, 0, 8);
        if (out0 && !out1) ++count_0;
        if (out1 && !out0) ++count_1;
        // Drop the gate so the next iteration sees a fresh rising edge.
        std::memset(bus, 0, sizeof(float) * 64 * 32);
        step_n_frames(loaded, alg, bus, 32);
    }
    // Allow ~5% tolerance on either side around 500.
    REQUIRE(count_0 >= 460);
    REQUIRE(count_0 <= 540);
    REQUIRE(count_1 >= 460);
    REQUIRE(count_1 <= 540);
    REQUIRE(count_0 + count_1 >= 950);  // some rolls may land on neither output
}
```

- [ ] **Step 2: Run test to verify it passes**

```bash
./build/host/test_hemispheres "[brancher]"
```

Expected: 9 test cases pass. If the fairness bounds fail, log the actual counts and widen the bounds to the next tier (440/560) only after confirming the seeded RNG is producing the expected sequence. Do not loosen tolerance to mask real bugs.

- [ ] **Step 3: Commit**

```bash
git add harness/tests/test_hemispheres.cpp
git -c commit.gpgsign=false commit -m "feat(test-applets): brancher B4 statistical fairness at p=50"
```

---

## Task 15: Final verification gate

**Files:**
- None (verification only).

- [ ] **Step 1: Run the full suite**

```bash
make clean
git submodule update --init --recursive
make arm
make test
```

Expected. `make arm` builds clean. `make test` runs `host` (sim binary), `test-applets` (22+ TEST_CASEs across `[smoke]`, `[calculate]`, `[brancher]`), then the YAML scenario. Final line:

```
All tests passed (N assertions in 22 test cases)
```

(The exact assertion count varies based on per-case REQUIRE count; 22 is the number of TEST_CASEs.)

- [ ] **Step 2: Run tagged subsets to verify isolation**

```bash
./build/host/test_hemispheres "[smoke]"
./build/host/test_hemispheres "[calculate]"
./build/host/test_hemispheres "[brancher]"
```

Expected: each command exits 0 and reports its subset of tests passing.

- [ ] **Step 3: Hardware sanity gate**

Build the ARM artifact and deploy to a real NT module to confirm Hemispheres still loads and Brancher / Calculate behave as before. Skip if no hardware available; flag in the PR description so a human can perform this step before merging.

```bash
make arm
make deploy-sysex
```

On the NT module:

- Misc, Plug-ins, View info: confirm Hemispheres lists with no failure flag.
- Add Hemispheres to a preset. Select Brancher on the left, Calculate on the right.
- Patch a gate into the left input. Observe gate routing changes when adjusting p.
- Patch CVs into right side inputs. Observe Calculate output reflecting MIN/MAX/SUM behavior across operation changes.

- [ ] **Step 4: No additional commit needed**

This task verifies prior commits. No file changes. Proceed to PR creation if hardware passes.

---

## Spec coverage check

| Spec section | Plan tasks |
|---|---|
| Architecture: Build pipeline | Task 1 |
| Architecture: Per-test wiring + Bus convention + Determinism | Task 1, Task 2 |
| Architecture: Test helper module | Task 2 (foundation), Task 3 (`pack_calculate`), Task 8 (`pack_brancher`) |
| Adding a new applet (Phase 2 onboarding) | Plan demonstrates the three steps in Tasks 3 (Calculate) and 8 (Brancher); Phase 2 follows the same pattern |
| Per-applet OnDataRequest bit layouts | Used in Tasks 3, 8 for current applets; documented in spec for Phase 2 |
| Implementation > Files created and modified | Tasks 1, 2 |
| Implementation > TDD order | Tasks 3-14 follow the spec's order (Calculate combinatorial -> S&H -> Rnd -> Brancher deterministic -> logical -> flip-flop -> UI -> statistical) |
| Test scenarios: Brancher B1-B9 | Tasks 8 (B1, B9), 9 (B2, B3), 10 (B5), 11 (B6), 12 (B7), 13 (B8), 14 (B4) |
| Test scenarios: Calculate C1-C13 | Tasks 3 (C1, C13), 4 (C2-C5), 5 (C6-C8), 6 (C9-C10), 7 (C11-C12) |
| Test scenarios: Smoke S1 | Task 1 |
| Verification > Local commands | Task 15 |
| Hardware re-verification gate | Task 15 step 3 |
