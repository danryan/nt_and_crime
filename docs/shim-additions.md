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
