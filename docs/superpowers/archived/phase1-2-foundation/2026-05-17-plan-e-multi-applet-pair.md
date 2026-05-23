# Plan E: Multi-Applet Pair (Phazerville Two-Hemisphere Layout)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans.

**Goal:** Allow a single NT plug-in to host two Hemisphere applets side-by-side on the 256x64 screen, replicating Phazerville O_C's left/right-hemisphere UX. One NT algorithm slot = one pair = two applets.

**Architecture:** A new `NT_HEM_PAIR(LeftKlass, RightKlass, guid, name, desc)` macro emits a plug-in factory whose `AlgorithmInstance` owns two applet instances. Each applet renders to its half of the screen (left 0..127, right 128..255). I/O is partitioned: channels A/B for left applet, C/D for right. The shim refactors `gfx_offset`, `HS::frame`, and `HS::HEM_SIDE` to be hemisphere-aware at runtime.

**Tech Stack:** C++11 (arm-none-eabi), existing shim from Plans B+C, distingNT_API v13.

**Pre-req:** Plan C complete (Plan B+C shim invariants are the foundation).

---

## Scope decisions

| Question | Decision | Reason |
|----------|----------|--------|
| Same applet class twice? | Yes (e.g., two Logic instances) | Hemisphere supports it; macro accepts arbitrary klass pair |
| Cross-hemisphere I/O? | No, each side independent | Matches Phazerville convention |
| UI orchestration | L encoder/button → left applet, R encoder/button → right applet | Most intuitive split |
| Persistence | Two 64-bit packed states per slot | Independent applet preservation |
| Single-applet plug-ins still ship? | Yes, `NT_HEM_PLUGIN` unchanged | Backwards compatible |

---

## Refactor surface

Plan E requires shim changes that affect single-applet plug-ins too. Each refactor is gated behind regression testing of all five Tier 1 applets before progressing.

### Shim invariants to change

1. `gfx_offset` from `#define 0` macro to a runtime `extern int hem_gfx_offset` (or static within `HemisphereApplet`) settable per-draw to 0 (left) or 128 (right).
2. `HS::frame` from single global instance to either:
   - Single instance with 4-channel-deep arrays (existing) where left uses [0..1], right uses [2..3].
   - Or two separate instances `HS::frame_left`, `HS::frame_right` selected via active-hemisphere context.
   Single-instance-with-channel-offset is preferred to minimize touchpoints in vendor code.
3. `HS::HEM_SIDE` already has `LEFT_HEMISPHERE = 0`. Add `RIGHT_HEMISPHERE = 1`. Bump `APPLET_CURSOR_COUNT` to 2.
4. `cvmap[]`, `trigmap[]` arrays already sized [4]; right applet's `In(0)`/`Gate(0)` should resolve to channel 2.
5. `HemisphereApplet::In()` already reads `cvmap[ch].In()`. The `ch` value is 0..1 in applet code. To remap for right hemisphere, intercept at base class: `In(int ch) { return cvmap[ch + (hemisphere * 2)].In(); }`.

---

## Task 1: Plumb hemisphere-aware I/O without breaking single-applet path

**Files:**
- Modify: `shim/include/HemisphereApplet.h`
- Modify: `shim/include/CVInputMap.h`
- Modify: `shim/include/HSIOFrame.h`

- [ ] **Step 1: Make `In()`/`Out()`/`Gate()`/`Clock()` channel-offset aware**

In `HemisphereApplet`, replace:

```cpp
int  In(int ch)        { return cvmap[ch].In(); }
bool Clock(int ch, bool = false) { return HS::frame.clocked[ch]; }
bool Gate(int ch)      { return trigmap[ch].Gate(); }
void Out(int ch, int value) { HS::frame.Out((DAC_CHANNEL)(ch), value); }
```

With:

```cpp
int  channel_offset() const { return hemisphere * 2; }
int  In(int ch)        { return cvmap[ch + channel_offset()].In(); }
bool Clock(int ch, bool = false) { return HS::frame.clocked[ch + channel_offset()]; }
bool Gate(int ch)      { return trigmap[ch + channel_offset()].Gate(); }
void Out(int ch, int value) { HS::frame.Out((DAC_CHANNEL)(ch + channel_offset()), value); }
```

`hemisphere` is the `HS::HEM_SIDE` member already set by `BaseStart()`. Left = 0 → offset 0. Right = 1 → offset 2.

- [ ] **Step 2: Bump APPLET_CURSOR_COUNT to 2**

```cpp
enum HEM_SIDE : uint8_t { LEFT_HEMISPHERE = 0, RIGHT_HEMISPHERE, APPLET_CURSOR_COUNT };
```

`cursor_countdown[]` and `enc_edit[]` arrays now sized 2.

- [ ] **Step 3: Run all Tier 1 tests on hardware**

Deploy Logic.o, AttenuateOffset.o, Slew.o, Calculate.o, Burst.o. Verify each still passes the validation table from Plan C. Channel offset for LEFT_HEMISPHERE is 0 — should be invariant.

- [ ] **Step 4: Commit**

```bash
git add shim/include/HemisphereApplet.h shim/include/HSUtils.h
git commit -m "shim: route HemisphereApplet I/O through hemisphere channel offset"
```

---

## Task 2: Make gfx_offset runtime

**Files:**
- Modify: `shim/include/HSUtils.h`
- Modify: `shim/src/globals.cpp`

- [ ] **Step 1: Replace macro with variable**

Remove `#define gfx_offset 0` from `HSUtils.h`. Add:

```cpp
namespace HS { extern int gfx_offset; }
using HS::gfx_offset;
```

In `globals.cpp`:

```cpp
namespace HS { int gfx_offset = 0; }
```

- [ ] **Step 2: Re-validate Tier 1**

`gfx_offset` defaults to 0; single-applet plug-ins unchanged. Verify Logic/Slew/Burst displays render identically.

- [ ] **Step 3: Commit**

```bash
git add shim/include/HSUtils.h shim/src/globals.cpp
git commit -m "shim: make gfx_offset a runtime variable for hemisphere-pair drawing"
```

---

## Task 3: Define NT_HEM_PAIR macro and pair instance

**Files:**
- Modify: `shim/include/hem_shim.h`

- [ ] **Step 1: Add pair instance template**

```cpp
template <typename L, typename R>
struct AlgorithmPairInstance : public _NT_algorithm {
    L left;
    R right;
    bool started = false;
};
```

- [ ] **Step 2: Add pair shim template**

A `PairShim<L, R>` struct mirroring `Shim<T>` but:
- `calculateRequirements`: `sizeof(AlgorithmPairInstance<L, R>)` + 16 parameters (8 routing params per side)
- `construct`: memset, placement-new, set parameters, call `BaseStart(LEFT_HEMISPHERE)` on left, `BaseStart(RIGHT_HEMISPHERE)` on right
- `step`: copy buses A/B for left, C/D for right; per-tick clock_countdown decrement on all 4 channels; call `Controller()` on each applet in turn (left then right); write outputs A/B and C/D back to buses
- `draw`: set `HS::gfx_offset = 0`, call `left.View()`; set `gfx_offset = 128`, call `right.View()`; reset to 0
- `customUi`: L encoder/button → forward to `left.OnEncoderMove`/`OnButtonPress`; R encoder/button → forward to `right`
- `serialise`: pack both states as `hem_left_hi/lo` + `hem_right_hi/lo`
- `deserialise`: parse all 4 keys, restore both applets independently

- [ ] **Step 3: Add the macro**

```cpp
#define NT_HEM_PAIR(L, R, guid_str_4chars, name_str, desc_str) \
    static const _NT_factory _hem_pair_factory = { \
        ... \
        .calculateRequirements = hem_shim::PairShim<L, R>::calculateRequirements, \
        .construct             = hem_shim::PairShim<L, R>::construct, \
        ... \
    }; \
    extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data) { ... }
```

- [ ] **Step 4: Build**

`make arm` should still pass for existing single-applet plug-ins.

- [ ] **Step 5: Commit**

```bash
git add shim/include/hem_shim.h
git commit -m "shim: add NT_HEM_PAIR macro and PairShim template"
```

---

## Task 4: Pair parameters and routing

**Files:**
- Modify: `shim/include/hem_shim.h`

The pair plug-in exposes 16 routing parameters:

- Gate (ch A) → default Input 1
- Gate (ch B) → default Input 2
- Gate (ch C) → default Input 3
- Gate (ch D) → default Input 4
- CV (ch A) → default Input 5
- CV (ch B) → default Input 6
- CV (ch C) → default Input 7
- CV (ch D) → default Input 8
- Out (ch A) → default Output 1 + mode (Replace)
- Out (ch B) → default Output 2 + mode (Replace)
- Out (ch C) → default Output 3 + mode (Replace)
- Out (ch D) → default Output 4 + mode (Replace)

Plus a "Routing" parameter page listing all 16.

- [ ] **Step 1: Add `pair_parameters()` returning the 16-entry array**

Mirrors single-applet `shim_parameters()` but with 4 channels.

- [ ] **Step 2: Adjust `step()` to copy/write 4 channels**

```cpp
copy_bus_to_frame(kParamCvInA, &HS::frame.inputs[0], ...);
copy_bus_to_frame(kParamCvInB, &HS::frame.inputs[1], ...);
copy_bus_to_frame(kParamCvInC, &HS::frame.inputs[2], ...);
copy_bus_to_frame(kParamCvInD, &HS::frame.inputs[3], ...);
// gates similarly
// outputs A/B from left.Out(), C/D from right.Out() via channel_offset()
```

- [ ] **Step 3: Build and run host tests**

- [ ] **Step 4: Commit**

```bash
git add shim/include/hem_shim.h
git commit -m "shim: pair plug-in routes 4 channels of I/O"
```

---

## Task 5: First pair plug-in (Logic + Calculate as canary)

**Files:**
- Create: `applets/LogicCalculate.cpp`
- Modify: `Makefile`

A test plug-in pairing two well-validated single applets.

- [ ] **Step 1: Wrapper**

```cpp
#include "hem_shim.h"
#include "Logic.h"
#include "Calculate.h"
NT_HEM_PAIR(Logic, Calculate, "HpLC", "Hem: Logic+Calc",
            "Logic on left hemisphere, Calculate on right")
```

- [ ] **Step 2: Makefile rule**

```makefile
build/arm/LogicCalculate.o: applets/LogicCalculate.cpp $(SHIM_DEPS)
	mkdir -p build/arm
	$(ARM_CXX) $(ARM_FLAGS) $(SHIM_INCLUDE) $(HEM_APPLET_INCLUDE) -c -o $@ $<
```

Append to `arm:` target.

- [ ] **Step 3: Build and deploy**

- [ ] **Step 4: Hardware verify**

- L encoder/button affects Logic on left half; R encoder/button affects Calculate on right.
- Gates on Inputs 1+2 drive Logic. CVs on Inputs 5+6 + clock on Input 3 drive Calculate.
- Out 1+2 = Logic outputs; Out 3+4 = Calculate outputs.

- [ ] **Step 5: Commit**

```bash
git add applets/LogicCalculate.cpp Makefile
git commit -m "applets: LogicCalculate pair plug-in canary"
```

---

## Task 6: Bookkeeping

- [ ] **Step 1: Append Round 4 to `docs/shim-additions.md`** documenting the pair refactor.
- [ ] **Step 2: Update spec progress.**

---

## Out of scope

- More than two applets per plug-in (NT can host multiple slots; users compose multi-pair flows that way).
- Cross-hemisphere routing (e.g., left's output into right's input). Each side is self-contained per Phazerville convention.
- Hemisphere "preset" config preset switching at runtime — single fixed pair per plug-in.

## Risk register

- **Vendor source assumes `LEFT_HEMISPHERE` and `RIGHT_HEMISPHERE`**: most applet code is hemisphere-symmetric, but some (e.g., Phazerville's preset menu) uses both. Audit applet sources for `RIGHT_HEMISPHERE` references that might break under our partitioning.
- **Static globals across applet instances**: any applet-local static variables in shim helpers (e.g., `prev_gate(idx)`) must be hemisphere-aware. Currently `prev_gate` has `static bool prev[2]`; needs bumping to `[4]` and using channel-offset index.
- **screen real estate**: Phazerville assumes 64-pixel-wide hemispheres on a 128-wide OLED. NT pair gets 128 pixels per hemisphere; gfxBitmap/gfxPrint positions might look stretched or sparse. Plan E renders them at native O_C scale (0..63), leaving room for clear separation column.
- **CPU**: two applets at NT step rate. Doubled `Controller` invocations per step. Likely fine for Tier 1 applets but worth profiling on the canary.
