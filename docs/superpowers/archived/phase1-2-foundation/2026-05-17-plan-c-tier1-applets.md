# Plan C: Tier 1 Applets (AttenuateOffset, Slew, Calculate, Burst)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the remaining four Tier 1 Hemisphere applets (AttenuateOffset, Slew, Calculate, Burst) to NT plug-ins, validated on hardware.

**Architecture:** Reuse the `hem_shim::Shim<T>` template and `NT_HEM_PLUGIN` macro from Plan B. Extend the shim with the additional helpers each applet needs (`simfloat`, `Proportion`, `StartADCLag`/`EndOfADCLag`, `random`, `HEMISPHERE_MIN_CV`). Each applet ships as one ARM object built from a 3-line wrapper. Verify on hardware after each port.

**Tech Stack:** C++11 (arm-none-eabi), distingNT_API v13, Phazerville Hemisphere applet sources (vendor/O_C-Phazerville).

---

## Pre-flight

Applet dependencies (from `vendor/O_C-Phazerville/software/src/applets/{AttenuateOffset,Slew,Calculate,Burst}.h`):

| Applet         | Needs |
|----------------|-------|
| AttenuateOffset | `In`, `Out`, `Proportion`, `HEMISPHERE_MAX_CV` (already present) |
| Slew           | `simfloat`, `int2simfloat`, `simfloat2int`, `Proportion`, `ProportionCV`, `In`, `Out`, `Gate`, `HEMISPHERE_MAX_INPUT_CV` |
| Calculate      | `Clock`, `In`, `Out`, `StartADCLag`, `EndOfADCLag`, `random`, `constrain`, `HEMISPHERE_MIN_CV`, `HEMISPHERE_MAX_CV` |
| Burst          | `Clock`, `In(0)`/`In(1)`, `DetentedIn`, `Out`/`ClockOut`, `StartADCLag`, `EndOfADCLag`, `random`, `Proportion` |

Shim gaps to close in Task 1.

---

## Task 1: Extend shim with simfloat + Proportion + ADC lag + random + HEMISPHERE_MIN_CV

**Files:**
- Modify: `shim/include/HSUtils.h`
- Modify: `shim/include/HemisphereApplet.h`
- Modify: `shim/include/hem_shim.h` (advance `adc_lag_countdown`)
- Modify: `shim/src/globals.cpp` (no change expected unless random needs state)

- [ ] **Step 1: Add macros to `HSUtils.h`**

Append after the existing `#define` block:

```cpp
#define HEMISPHERE_MIN_CV (-(PULSE_VOLTAGE * ONE_OCTAVE))
#define HEMISPHERE_ADC_LAG 96
#ifndef int2simfloat
#define int2simfloat(x) ((int32_t)(x) << 14)
#define simfloat2int(x) ((int32_t)(x) >> 14)
using simfloat = int32_t;
#endif
```

- [ ] **Step 2: Add `Proportion` + ADC lag + `random` to `HemisphereApplet.h`**

Add to the public section of `HemisphereApplet`:

```cpp
int Proportion(int numerator, int max_n, int max_p) const {
    if (max_n == 0) return 0;
    long p = (long)numerator * max_p / max_n;
    return (int)p;
}

void StartADCLag(int ch = 0, int lag = HEMISPHERE_ADC_LAG) {
    HS::frame.adc_lag_countdown[ch] = lag;
}
bool EndOfADCLag(int ch = 0) {
    return (--HS::frame.adc_lag_countdown[ch] == 0);
}
```

- [ ] **Step 3: Add `random()` shim**

Append to `HemisphereApplet.h` (free function in global scope, outside class):

```cpp
inline int hem_shim_random(int min_inclusive, int max_exclusive) {
    extern uint32_t hem_rng_state;
    hem_rng_state ^= hem_rng_state << 13;
    hem_rng_state ^= hem_rng_state >> 17;
    hem_rng_state ^= hem_rng_state << 5;
    int range = max_exclusive - min_inclusive;
    if (range <= 0) return min_inclusive;
    return min_inclusive + (int)(hem_rng_state % (uint32_t)range);
}
#define random(...) hem_shim_random(__VA_ARGS__)
```

Define `hem_rng_state` in `shim/src/globals.cpp`:

```cpp
uint32_t hem_rng_state = 0x12345678u;
```

- [ ] **Step 4: Rebuild Logic.o to confirm regression-free**

```bash
make arm
```

Expected: build clean, no warnings.

- [ ] **Step 5: Commit**

```bash
git add shim/include/HSUtils.h shim/include/HemisphereApplet.h shim/src/globals.cpp
git commit -m "shim: add simfloat, Proportion, ADC lag, random for Tier 1 applets"
```

---

## Task 2: AttenuateOffset wrapper

**Files:**
- Create: `applets/AttenuateOffset.cpp`
- Modify: `Makefile` (add object rule)

- [ ] **Step 1: Write wrapper**

`applets/AttenuateOffset.cpp`:

```cpp
#include "hem_shim.h"
#include "AttenuateOffset.h"
NT_HEM_PLUGIN(AttenuateOffset, "HAO1", "Hem: Attenuate/Offset",
              "Phazerville Hemisphere AttenuateOffset applet")
```

- [ ] **Step 2: Add Makefile rule**

In `Makefile`, after the `Logic.o` rule, add:

```makefile
build/arm/AttenuateOffset.o: applets/AttenuateOffset.cpp $(wildcard shim/include/*.h) $(wildcard shim/include/*/*.h) $(wildcard shim/src/*.cpp)
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -c -o $@ $<
```

And update the `arm:` target:

```makefile
arm: build/arm/gainCustomUI.o build/arm/gain.o build/arm/bus_probe.o build/arm/Logic.o build/arm/AttenuateOffset.o
```

- [ ] **Step 3: Build**

```bash
make arm
```

Expected: `build/arm/AttenuateOffset.o` produced, no warnings.

- [ ] **Step 4: Commit**

```bash
git add applets/AttenuateOffset.cpp Makefile
git commit -m "applets: AttenuateOffset.cpp wrapper for Phazerville AttenuateOffset applet"
```

---

## Task 3: AttenuateOffset hardware verification

**Files:** none modified; user-driven test.

- [ ] **Step 1: Deploy**

Upload `build/arm/AttenuateOffset.o` via nt_helper (preferred) or USB MSC. Reboot NT.

- [ ] **Step 2: Patch**

- Patch +5V source (LFO, sequencer, or steady 5V) into NT physical Input 1
- Scope ch A on physical Output 1

- [ ] **Step 3: Walk attenuation table**

Default Out1 attenuation = 100%, offset = 0V. Cycle attenuation via R-encoder (in edit mode).

Fill measured values:

```markdown
| Atten % | Offset | Input | Out1 expected | Out1 actual |
|---------|--------|-------|---------------|-------------|
| 100     | 0      | +5V   | +5V           |             |
| 50      | 0      | +5V   | +2.5V         |             |
| 0       | 0      | +5V   | 0V            |             |
| 100     | +1V    | +5V   | +6V (clamp)   |             |
| 100     | -1V    | +5V   | +4V           |             |
| -100    | 0      | +5V   | -5V           |             |
```

- [ ] **Step 4: Pass criterion**

All measured values within ±0.2V of expected. Document any miss in `docs/hardware-deploy.md` or scope cuts.

---

## Task 4: Slew wrapper

**Files:**
- Create: `applets/Slew.cpp`
- Modify: `Makefile`

- [ ] **Step 1: Wrapper**

```cpp
#include "hem_shim.h"
#include "Slew.h"
NT_HEM_PLUGIN(Slew, "HSlw", "Hem: Slew",
              "Phazerville Hemisphere Slew applet")
```

- [ ] **Step 2: Makefile rule + arm target**

Add `build/arm/Slew.o` rule mirroring Task 2 Step 2. Append to `arm:` target.

- [ ] **Step 3: Build**

```bash
make arm
```

- [ ] **Step 4: Commit**

```bash
git add applets/Slew.cpp Makefile
git commit -m "applets: Slew.cpp wrapper for Phazerville Slew applet"
```

---

## Task 5: Slew hardware verification

- [ ] **Step 1: Deploy** Slew.o, reboot.

- [ ] **Step 2: Patch**

- Square LFO (0V/+5V, ~1Hz) into Input 1
- Scope ch A on Output 1

- [ ] **Step 3: Visual check**

| Rise rate | Expected scope shape |
|-----------|----------------------|
| Min (slowest)  | Linear ramp over many seconds; never reaches +5V if LFO period < ramp time |
| Mid            | Visible ramp during LFO transition |
| Max (instant)  | Square wave unchanged |

R-encoder adjusts rate. Confirm ramp slope changes monotonically with encoder turn.

- [ ] **Step 4: Pass criterion**

Ramp visible at low rates; instantaneous at max rate. No NaN/glitch behavior.

---

## Task 6: Calculate wrapper

**Files:**
- Create: `applets/Calculate.cpp`
- Modify: `Makefile`

- [ ] **Step 1: Wrapper**

```cpp
#include "hem_shim.h"
#include "Calculate.h"
NT_HEM_PLUGIN(Calculate, "HCal", "Hem: Calculate",
              "Phazerville Hemisphere Calculate applet")
```

- [ ] **Step 2: Makefile rule + arm target**

Same pattern as previous wrappers.

- [ ] **Step 3: Build**

```bash
make arm
```

- [ ] **Step 4: Commit**

```bash
git add applets/Calculate.cpp Makefile
git commit -m "applets: Calculate.cpp wrapper for Phazerville Calculate applet"
```

---

## Task 7: Calculate hardware verification

- [ ] **Step 1: Deploy** Calculate.o, reboot.

- [ ] **Step 2: Patch**

- CV source A (e.g., +2V from LFO offset, or a steady voltage) → Input 1
- CV source B → Input 2
- Gate trigger (LFO square or button gate) → Gate In 1 and Gate In 2 (or daisy-chained)
- Scope ch A → Output 1, ch B → Output 2

- [ ] **Step 3: Walk ops**

Calculate ops include SUM/DIFF/MAX/MIN/QUANTIZE/RANDOM (see `Calculate.h` near line 30).

| Op (Out1) | Op (Out2) | In1   | In2   | Trigger | Out1 expected | Out1 actual | Out2 expected | Out2 actual |
|-----------|-----------|-------|-------|---------|---------------|-------------|---------------|-------------|
| SUM       | DIFF      | +2V   | +1V   | rising  | +3V           |             | +1V           |             |
| MAX       | MIN       | +2V   | +1V   | rising  | +2V           |             | +1V           |             |
| RAND      | RAND      | any   | any   | rising  | random        |             | random        |             |

- [ ] **Step 4: Pass criterion**

Arithmetic ops within ±0.2V. RAND produces a new value each trigger.

---

## Task 8: Burst wrapper

**Files:**
- Create: `applets/Burst.cpp`
- Modify: `Makefile`

- [ ] **Step 1: Wrapper**

```cpp
#include "hem_shim.h"
#include "Burst.h"
NT_HEM_PLUGIN(Burst, "HBst", "Hem: Burst",
              "Phazerville Hemisphere Burst applet")
```

- [ ] **Step 2: Makefile rule + arm target**

Same pattern.

- [ ] **Step 3: Build**

```bash
make arm
```

- [ ] **Step 4: Commit**

```bash
git add applets/Burst.cpp Makefile
git commit -m "applets: Burst.cpp wrapper for Phazerville Burst applet"
```

---

## Task 9: Burst hardware verification

- [ ] **Step 1: Deploy** Burst.o, reboot.

- [ ] **Step 2: Patch**

- Trigger source (single-shot button or slow LFO square) → Gate In 1
- Scope ch A → Output 1 (burst gate train)
- Scope ch B → Output 2 (end-of-burst trigger)

- [ ] **Step 3: Walk parameters**

Default: number=1 burst. Cycle via encoders:

| Number | Spacing (ticks) | Expected scope |
|--------|-----------------|----------------|
| 3      | 50              | 3 short gates spaced ~3ms apart at Output 1; single gate at Output 2 after burst |
| 6      | 100             | 6 gates spaced ~6ms apart |
| 1      | n/a             | 1 gate echo on each trigger |

- [ ] **Step 4: Pass criterion**

Number of gates matches setting. Spacing scales monotonically with spacing param. End-of-burst gate fires once per trigger.

---

## Task 10: Bookkeeping and close

- [ ] **Step 1: Update spec progress section**

In `docs/superpowers/specs/2026-05-16-nt-hemisphere-shim-design.md`, mark Tier 1 complete.

- [ ] **Step 2: Update task list**

Mark task #9 (Tier 1 validation) complete via TaskUpdate. Move to Plan D or skip to Tier 2 per user direction.

- [ ] **Step 3: Final commit**

```bash
git add docs/superpowers/specs/2026-05-16-nt-hemisphere-shim-design.md
git commit -m "docs: mark Tier 1 applets complete"
```

---

## Out of scope

- Bitmap-icon rendering quality (deferred to Plan D)
- Tier 2 applets (Brancher, TLNeuron, GateDelay) — deferred to a follow-up plan
- Multi-applet support (each applet ships as its own .o)

## Risk register

- **Slew `simfloat` precision**: 14-bit fixed-point. Coarse at small voltages. Acceptable if visible ramps render correctly.
- **Burst timing accuracy**: Hemisphere assumes ~16kHz Controller tick. NT step buffer cadence may differ. If gate spacing drifts, expose Controller frequency as a shim parameter or compensate.
- **`random()` seed determinism**: PRNG state init from constant. Bursts/Calculate-RAND will be reproducible across reboots, not truly random. Acceptable for musical use.
- **ADC lag countdown drift**: shim decrements once per Controller call (one per step). If NT step rate differs from Hemisphere expectation, ADC lag duration changes. Verify on hardware; tune `HEMISPHERE_ADC_LAG` if needed.
