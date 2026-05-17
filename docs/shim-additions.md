# Shim Additions Log

Per-applet ledger of what each Hemisphere applet required beyond the previous shim baseline. Goal: track surface area so the shim can be audited and minimized over time.

The Hemisphere source code is vendored unmodified. Every entry here is shim code or a placeholder definition the applet expects but which the shim must provide.

## Baseline (Plan B Task 1-7, Logic applet)

Created from scratch to make a single applet compile and run on NT.

### Types and constants

- `HS::IOFrame` with `inputs[4]`, `outputs[4]`, `clocked[4]`, `changed_cv[4]`, `cycle_ticks[4]`, `adc_lag_countdown[4]`, `tick`
- `HS::OutputCell { value, target }` with `set()` and `get_target()`
- `HS::HEM_SIDE` enum (LEFT_HEMISPHERE), `HS::APPLET_CURSOR_COUNT`, `HS::HELP_SECTIONS`
- `PackLocation`, `Pack`, `Unpack` (inline, not constexpr due to C++11)
- `HEMISPHERE_MAX_CV`, `HEMISPHERE_MAX_INPUT_CV`, `HEMISPHERE_CENTER_INPUT_CV`, `HEMISPHERE_CENTER_DETENT`, `HEMISPHERE_CLOCK_TICKS`, `HEMISPHERE_CURSOR_TICKS`, `ONE_OCTAVE`, `PULSE_VOLTAGE`
- `ForEachChannel(ch)` macro, `gfx_offset` macro, `io_offset` macro

### Globals

- `HS::frame` IOFrame instance
- `HS::help_strings[]`, `HS::cursor_countdown[]`, `HS::enc_edit[]`
- `OC::CORE::ticks`
- `OC::Strings::capital_letters[]`
- `cvmap[4]`, `trigmap[4]` (CVInputMap, DigitalInputMap)
- `clock_m` (HSClockManager stub)

### Classes

- `HemisphereApplet` base with `Start()`, `Controller()`, `View()`, `OnEncoderMove()`, `OnDataRequest()`, `OnDataReceive()`, `OnButtonPress()`, `AuxButton()`, `BaseStart()`, `CursorBlink()`, `ResetCursor()`, `CursorToggle()`, `EditMode()`, `MoveCursor()`
- I/O API: `In()`, `ViewIn()`, `ViewOut()`, `Clock()`, `Gate()`, `DetentedIn()`, `Out()`, `ClockOut()`, `GateOut()`, `ProportionCV()`
- gfx API: `gfxPos`, `gfxPrint` (string/int), `gfxFrame`, `gfxRect`, `gfxInvert`, `gfxClear`, `gfxLine`, `gfxPixel`, `gfxCircle`, `gfxBitmap`, `gfxIcon`, `gfxCursor`
- `CVInputMap`, `DigitalInputMap` with explicit constructors (C++11 aggregate-init workaround)
- `shim::Graphics` rendering class with `setPrintPos`, `print`, `setPixel`, `drawLine`, `drawFrame`, `drawRect`, `invertRect`, `clearRect`, `drawCircle`, `drawBitmap8`

### Plug-in machinery (`hem_shim.h`)

- `hem_shim::Shim<T>` template wrapping NT `_NT_algorithm` lifecycle
- `NT_HEM_PLUGIN(klass, guid, name, desc)` macro generating `_NT_factory` and `pluginEntry`
- 8 routing parameters: Gate In 1/2, CV In 1/2, CV Out 1/2 with mode
- `serialise`/`deserialise` packing 64-bit applet state into JSON members

### Icons (placeholders, 8 bytes column-major)

- `ZAP_ICON`, `CV_ICON`, `DOWN_BTN_ICON`
- `PhzIcons::logic`

### Misc

- `Arduino.h`: templated `constrain`, `min`/`max` macros
- `OC_gpio.h`: `NorthernLightModular = 0`
- `util_math.h`: `CONSTRAIN` macro
- `OC_DAC.h`: `DAC_CHANNEL` enum

## Round 2 (Plan B post-hardware)

Hardware testing of Logic exposed three correctness bugs.

| Change | Why |
|--------|-----|
| `HS::IOFrame::gate_high[4]` added; `DigitalInputMap::Gate()` reads it | Hemisphere convention: `clocked[]` is edge-detect, `gate_high[]` is level-detect. Previously `Gate()` returned edges, so steady-high inputs never registered. |
| `read_gate` returns `{rising, high}` and writes both fields | One bus scan produces both signals. |
| Gate threshold lowered `1.0f` → `0.5f` | bus_probe Level=100 writes exactly 1.0f; strict `>` never triggered. |
| Default routing: Gate In 1+2 → Input 1+2, CV Out 1+2 → bus 13+14 | Was 3+4/15+16; physical jacks 1-4/Out 1-2 are the obvious default. |
| CV Out mode default = Replace (overrode API macro) | Logic is a discrete gate generator, not a CV summer. |
| `graphics.cpp::print()` calls `NT_drawText(x, y+7)` | Replaced solid-block placeholder font. Off-by-baseline +7 brings NT top-aligned. |

## Plan C Task 1 (baseline for any Tier 1 applet)

Helpers every Tier 1 applet but Logic needed.

| Symbol | Provided in | Reason |
|--------|-------------|--------|
| `simfloat`, `int2simfloat`, `simfloat2int` | `HSUtils.h` | Slew uses fixed-point math |
| `HEMISPHERE_MIN_CV` | `HSUtils.h` | Calculate clamps to it |
| `HEMISPHERE_ADC_LAG` | `HSUtils.h` | StartADCLag default |
| `HemisphereApplet::Proportion(num, max_n, max_p)` | `HemisphereApplet.h` | Burst/Calculate scale CV inputs |
| `StartADCLag`, `EndOfADCLag` | `HemisphereApplet.h` | Calculate/Burst sample input with delay after clock |
| `random()` macro + `hem_shim_random` (xorshift) | `HemisphereApplet.h` | Burst jitter, Calculate RAND op |
| `hem_rng_state` | `globals.cpp` | PRNG state |

## Per-applet additions

### AttenuateOffset

| Symbol | Where | Reason |
|--------|-------|--------|
| `constrain` accepts mixed types (`int`, `bool`, `bool`) | `Arduino.h` | Applet calls `constrain(Unpack(...), false, true)`. Single-T template failed deduction. |
| `UP_ICON`, `DOWN_ICON`, `UP_DOWN_ICON` | `HSicons.h` + `icons.cpp` | Drawn in cursor/edit mode |
| `gfxPrintVoltage(int cv)` | `HemisphereApplet.h` | DrawInterface prints offset value |
| `PhzIcons::dualAttenuverter` | `PhzIcons.h` + `icons.cpp` | `applet_icon()` return value (placeholder bitmap) |

### Slew

| Symbol | Where | Reason |
|--------|-------|--------|
| `gfxLine(x, y, x2, y2, bool dashed)` 5-arg overload | `HemisphereApplet.h` | DrawIndicator distinguishes selected vs idle lines |
| `Graphics::drawLine(..., uint8_t pattern)` 8-bit pattern arg | already existed | Dashed dispatches to it |
| `PhzIcons::slew` | placeholder | `applet_icon()` |

### Calculate

| Symbol | Where | Reason |
|--------|-------|--------|
| `gfxSkyline()` method | `HemisphereApplet.h` | View() decoration |
| `BottomAlign(h)` macro | `HSUtils.h` | gfxSkyline uses it |
| `CLOCK_ICON` | `HSicons.h` + `icons.cpp` | Drawn next to op name when in clocked mode |
| `PhzIcons::calculate` | placeholder | `applet_icon()` |

### Burst

| Symbol | Where | Reason |
|--------|-------|--------|
| `GAUGE_ICON`, `RANDOM_ICON` | `HSicons.h` + `icons.cpp` | DrawSelector shows them for spacing+jitter rows |
| `PhzIcons::burst` | placeholder | `applet_icon()` |

## Round 3 (Plan C post-hardware, all four applets)

Global shim invariants discovered through Tier 1 hardware testing. Each fix benefits all current and future applets.

| Change | Why |
|--------|-----|
| `memset(ptrs.sram, 0, sizeof(AlgorithmInstance<T>))` before placement-new | Hemisphere applets rely on O_C's pre-zeroed RAM. NT SRAM is uninitialized. Trivial members declared without initializers (e.g., AttenuateOffset's `int offset[2]`) held garbage, producing wildly wrong outputs. |
| Routing params renamed: `Gate (ch A/B)`, `CV (ch A/B)`, `Out (ch A/B)` | Old `Gate In 1` / `CV In 1` numbering misled users into thinking the trailing number was a physical NT input number. Channel-letter form makes the channel/input distinction explicit. |
| Default bus assignments aligned with O_C jack layout: Inputs 1+2 → Gate ch A+B, Inputs 5+6 → CV ch A+B, Outputs 1+2 → Out ch A+B | O_C has 4 trigger inputs (1-4) and 4 CV inputs (5-8). NT mirroring the layout lets users transpose patches by jack number. |
| `BaseStart()` moved from lazy first-step path into `construct()` | Start() previously ran on first step() — after deserialise restored saved state — wiping the restored fields with defaults. Order is now: construct → Start (defaults) → deserialise (restore overrides) → step. |
| `deserialise` guards `OnDataReceive` behind `found_hi && found_lo` | NT calls deserialise even for fresh slots with empty JSON. With state=0 the AttenuateOffset unpack-with-bias formula (`Unpack() - 256`) computed offset=-256 (clamped to -72), making a "fresh" slot show offset=-6V. Now empty deserialise leaves the Start() defaults intact. |
| Per-step Controller multiplier: `numFrames / 3` calls per `step()` | Hemisphere code assumes ~16.66kHz Controller calls (60us tick). NT runs `step()` once per audio buffer (~3kHz at 16-sample buffer). Without compensation, Slew's `HEM_SLEW_MAX_TICKS`-based time constants ran 5x too slow vs displayed ms. Calling Controller proportionally more often per buffer aligns wall time with applet expectations. |
| `Graphics::drawLine` honors `pattern` byte | Previously pattern arg was ignored; all lines were solid. Slew's rise/fall cursor indicator uses dashed lines via this pattern; now dashes are visible. Default pattern bumped from `1` to `0xFF`. |
| `HS::IOFrame::clock_countdown[4]` added; per-tick decrement zeroes output on expiry | Upstream `ClockOut` sets countdown + high; tick handler decrements and lowers output at zero. Shim's old `ClockOut` set high without auto-low, so Burst pulses 2..N latched and only the first was visible. |

## Per-applet additions (Tier 1)

Symbols each applet contributed beyond the global helpers above.

## Round 4 (Plan D investigation, all Tier 1 applets)

User reports: icons misshapen, bottom of screen cut off, count squares non-uniform. One root cause hit by three symptoms.

| Change | Why |
|--------|-----|
| `set_pixel` and `invertRect` nibble convention inverted: even x = HIGH nibble, odd x = LOW nibble | NT framebuffer convention is opposite the initial guess. Old code paired writes to wrong nibble, swapping pixel columns within each byte. Bitmaps rendered with cols (0..7) showing at positions (1,0,3,2,5,4,7,6). Logic-gate icons and clock/gauge/random glyphs looked scrambled; 4x4 frame outlines drew with stray inner pixels because corner pixels landed in adjacent bytes. Single fix solves icon legibility + count-square uniformity. |
| `draw()` returns `true` to claim full screen | Returning `false` lets NT firmware overlay parameter line over applet output. Doc says "top" but observation showed bottom rows of Burst (count squares, /div text) covered. Returning true suppresses overlay; applet owns all 256x64. |
| `memset(NT_screen, 0, 128*64)` before each `View()` | With `draw()` returning true, NT no longer clears screen between frames. Stale pixels from prior cycles accumulate. Burst's varying `bursts_to_go` caused outline accumulation; explicit clear keeps frames clean. |

### Plan D Path B (vectorize drawBitmap8 via `NT_drawShapeI`) — abandoned

Initially hypothesised that emitting `kNT_rectangle` calls would improve legibility via NT's drawing primitives. After the nibble-convention root cause was identified, `setPixel`-based `drawBitmap8` renders correctly. Rect-coalesce emits identical pixels at this scale. Reverted to keep the path simple.

Plan D's three candidate paths (A: leave as-is, B: vectorize, C: hand-craft) all became unnecessary; the original assessment that icons were "misshapen" was a rendering bug, not a design issue.

## Observations

- The C++11 `constrain` polymorphism issue is recurring. Three argument types make it brittle; consider a non-template Arduino-style macro if more applets hit this.
- Icons accumulate steadily (3 → 9 → 10 over four applets). Audit feasible: a `HS_ICON_TABLE` generated from a list of names would replace the manual declaration + definition pairs.
- `PhzIcons::*` is a stable extension point. Each applet just needs one new placeholder. Could autogenerate from a list, or accept that adding a new placeholder is the marginal cost of a new applet.
- No applet so far has required `gfxHeader`, `gfxBitmap` with non-8 heights, or any Phazerville `HSApplication`-derived behavior. If a Tier 2 applet needs these, the shim grows again.

## Files touched cumulatively

```
shim/include/Arduino.h
shim/include/HSIOFrame.h
shim/include/HSUtils.h
shim/include/HSicons.h
shim/include/HemisphereApplet.h
shim/include/CVInputMap.h
shim/include/PhzIcons.h
shim/include/hem_graphics.h
shim/include/hem_shim.h
shim/include/hem_shim_impl.h
shim/src/globals.cpp
shim/src/graphics.cpp
shim/src/icons.cpp
```

13 files. The shim is the entire NT-side adaptation; vendor source unchanged.
