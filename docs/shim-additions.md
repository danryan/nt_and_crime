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

## Round 5 (Plan E multi-applet pair)

One NT slot hosts two applets side-by-side, replicating Phazerville O_C left/right hemisphere layout. Single-applet path remains unchanged.

| Change | Why |
|--------|-----|
| `HS::HEM_SIDE` adds `RIGHT_HEMISPHERE`; `APPLET_CURSOR_COUNT` becomes 2 | Pair needs two cursor and edit-state slots. `cursor_countdown[]` and `enc_edit[]` auto-resize. |
| `HemisphereApplet::channel_offset()` = `hemisphere * 2` | Applet code reads/writes channels 0/1; offset shifts to 0/1 (left) or 2/3 (right) at the base class. Applet source untouched. |
| `In/Clock/Gate/Out/ViewIn/ViewOut/StartADCLag/EndOfADCLag` route through `ch + channel_offset()` | All per-channel I/O paths funnel through one offset. Left hemisphere offset = 0; single-applet shim behaviour preserved. |
| `gfx_offset` promoted from `#define 0` macro to `extern int HS::gfx_offset` | Pair draw flips it 0 → 128 between left and right `View()` calls. Single-applet path keeps default 0. |
| `NT_HEM_PAIR(LeftKlass, RightKlass, guid, name, desc)` macro + `PairShim<L, R>` template | Mirrors `NT_HEM_PLUGIN`/`Shim<T>`. 16 routing parameters (4 gates, 4 CVs, 4 outs + modes). Tick loop calls `left.Controller()` then `right.Controller()`. Draw memsets, sets `gfx_offset = 0`, calls `left.View()`, sets `gfx_offset = 128`, calls `right.View()`. |
| Pair `customUi` routes L encoder/button to left applet, R encoder/button to right applet, no forced `isEditing` toggle | Each side's edit mode is owned by its own button-toggled `enc_edit[side].isEditing`. Differs from single-applet path where R encoder is always "edit-mode" (cursor moves on L, value adjusts on R). |
| Pair `serialise`/`deserialise` writes/reads `hem_left_hi/lo` and `hem_right_hi/lo` | Both 64-bit applet states preserved independently. |
| `PairShim::prev_gate` sized 4 (vs single-applet `Shim::prev_gate` sized 2) | Pair tracks four input gate edges. |

### Routing collision pitfall

Loading multiple shim plug-ins (single or pair) in the same NT preset means all instances default to the same input/output buses. The user must re-route each slot. Pair plug-ins reduce the problem (one slot = two applets) but do not eliminate it for presets that combine multiple shim slots.

## Round 6 (Plan F runtime applet selector)

Replaces all per-applet plug-ins and the LogicCalculate pair canary with a single `Hemispheres` plug-in (GUID `hemi`). Two applet selectors (Left, Right) exposed as enum parameters drive live swap at runtime.

| Change | Why |
|--------|-----|
| Single plug-in `Hemispheres` replaces `Hl01`, `HAO1`, `HSlw`, `HCal`, `HBst`, `HpLC` | One binary serves all five applets in any pair combination plus single-side-Empty. Drops menu clutter and binary count. |
| Applet selection via parameters, not NT specifications | Specs are add-time only. Parameters let user swap applets without removing + re-adding slot. Preserves UX of live exploration. |
| Empty applet as default + sentinel | Default both sides = Empty. Zero CPU until user picks. Screen shows "Pick applet" hint per side, self-documenting onboarding. |
| Polymorphic `HemisphereApplet*` storage with `kMaxAppletSize` worst-case sram per side | Live swap requires runtime polymorphism. C++11 constexpr `cmax` chain bounds sram per side. |
| Live swap sequence in `step()` head | Detect cached-vs-live selector diff. On change: dtor old, zero side I/O (outputs, clock_countdown, cursor, edit), placement-new new applet, BaseStart, cache new idx. |
| Setup + Routing parameter pages | Setup holds 2 selectors (rare-edit). Routing holds 16 I/O params (frequent-edit). |
| Serialise persists selector indices alongside applet state | Deserialise reconstructs applets first (so state lands in correct class), then feeds 64-bit `OnDataReceive` per side. |
| Retired `Shim<T>`, `NT_HEM_PLUGIN`, `PairShim`, `NT_HEM_PAIR`, pair param machinery | Replaced wholesale. Helpers (`copy_bus_to_frame`, `read_gate`, `write_frame_to_bus`) extracted to `hem_shim::` namespace free functions. |
| `applets/Hemispheres.cpp` is the sole TU | Single .cpp file pulls vendor headers via `HemispheresFactory.h`. No partial linking, no adapter files. Simpler build. Per-applet glue files can be re-introduced if a real need shows up. |
| `HSUtils.h` externs moved into `namespace HS { }` | Matches `globals.cpp` definitions. Was a long-standing mismatch tolerated by per-plugin builds (each pulled `hem_shim_impl.h` which inlined `globals.cpp` into its TU). Adapter pattern can no longer rely on that workaround. |

### Sram budget

Each Hemispheres slot allocates `2 * round_up(kMaxAppletSize, kMaxAppletAlign)` bytes for applet storage. Hemisphere applets pack persistent state into a 64-bit blob via `OnDataRequest`; runtime structs add a handful of cursor and CV scratch fields. Total per slot stays sub-KB even at the full applet enum.

### Flash cost

Every applet linked into `Hemispheres.o` adds its compiled code (typically a few KB per applet). At full enum the plug-in binary is on the order of ~50-150 KB. NT plug-in storage handles this without issue.

### Retired-plug-in artifacts

Pre-Plan-F `.o` files (`Logic.o` etc.) on NT devices remain functional but are no longer rebuilt. `make clean` removes them locally. Going forward, only `Hemispheres.o` ships.

### Routing collision pitfall (unchanged from Round 5)

Loading multiple `Hemispheres` slots in the same preset still defaults all instances to the same I/O buses. User must re-route per slot.

## Round 7 (Plan G Tier 2 applet expansion)

Grows the runtime `Hemispheres` enum from 6 entries to 14 by porting 8 Tier 2 applets in a single sweep. Selector serialisation switches from raw enum index to applet-name strings so presets stop being coupled to enum order. No new shim files; all additions land in existing headers and translation units.

| Change | Why |
|--------|-----|
| `probe_applet.sh` harness drives per-applet compile audit | Each candidate applet gets a one-shot script that adds it to the factory, compiles `Hemispheres.o`, lists missing symbols via `nm`, and reverts. Drives a tight pick-applet -> see-required-stubs loop without polluting `main`. |
| 8 Tier 2 applets added in one round | Brancher, TLNeuron, GateDelay, Button, ClkToGate, Compare, Cumulus, GatedVCA. Each ports cleanly with sub-10-LOC shim deltas. |
| Selector serialise switched to applet-name strings | Decouples preset format from `kApplet*` enum ordinals. Adding an applet no longer renumbers existing presets. |
| One Tier 2 applet deferred (Binary) | Vendor implementation depends on a non-trivial `SegmentDisplay` 7-segment renderer that exceeds the inline-stub budget. Tier 3 task: port `SegmentDisplay` as a dedicated shim module. |

### Applets added (8)

| Applet | Category | Stubs added |
|--------|----------|-------------|
| Brancher | Clocking | `Modulate<T>` template member on `HemisphereApplet`, `pad(range, number)` helper in `HS` namespace, `PhzIcons::brancher` placeholder |
| TLNeuron | Logic | `PhzIcons::thresholdLogicNeuron` placeholder, 5-arg `gfxDottedLine(x, y, x2, y2, density)` overload, `byte` typedef in `Arduino.h` |
| GateDelay | Utility | `PhzIcons::gateDelay` placeholder (`CLOCK_ICON` already present) |
| Button | Utility | `ROTATE_R_ICON`, `ROTATE_L_ICON`, `CLOSED_ICON`, `OPEN_ICON` glyphs, `PhzIcons::button2` placeholder |
| ClkToGate | Utility | `GATE_ICON`, `MOD_ICON` glyphs, `gfxCursor(int, int, int, const char*, const char*)` label overload, `ClockCycleTicks(int)` accessor |
| Compare | Utility | `PhzIcons::compare` placeholder |
| Cumulus | Modulator | `PhzIcons::cumulus` placeholder |
| GatedVCA | Utility | `PhzIcons::gateVca` placeholder, 2-arg `gfxPrint(int x_adv, int num)` padding overload |

Each applet went through the same loop: pick from vendor `software/src/applets/`, run `probe_applet.sh` to enumerate missing symbols, add stubs to the appropriate shim header or source file, re-run the harness until the compile is clean, then verify via `nm` symbol checks and host `gainCustomUI`/`zero_signal.yaml` regression. Hardware verification is deferred to a single post-Plan-G batch run.

### Selector serialisation switched to applet-name strings

Plan F serialised the two side selectors as raw `int` enum indices in JSON members `sel_l` and `sel_r`. Adding or reordering applets renumbered the enum, which silently broke saved presets. Round 7 switches to writing the applet name string via `_NT_jsonStream::addString`. Deserialise reads the name with `parse.string(...)`, resolves it to an index via a new `applet_index_for_name(const char*)` helper in `HemispheresFactory.h`, and dispatches `hemispheres_swap`. The helper uses a hand-rolled `strcmp` over the existing applet-name table; no new libc surface area.

No back-compat shim for old int-form presets. A Plan F preset hitting the new code fails `parse.string(...)` and the selector stays at its default (Empty). The number of in-flight Plan F presets is small and the fail-soft behavior is obvious to the user. Documented and accepted.

### Applets deferred to Tier 3

| Applet | Vendor name | Blocker |
|--------|-------------|---------|
| Binary | BinaryCtr | Depends on `software/src/SegmentDisplay.h`, a stateful 7-segment-digit renderer with position state, size variants, and pixmap emission. Inline stub would exceed the 10-LOC per-applet shim budget. Port `SegmentDisplay` as a dedicated shim module first, then revisit. `HEMISPHERE_3V_CV` constant and `PhzIcons::binaryCounter` placeholder are both trivial; `SegmentDisplay` is the single real blocker. |

### Plug-in size impact

| Metric | Plan F final | After GateDelay (Task 3) | Plan G final (after stretch) |
|--------|--------------|--------------------------|------------------------------|
| `Hemispheres.o` text | 12009 B | 12937 B | 17426 B |
| `Hemispheres.o` data | 1248 B | 1320 B | 1700 B |
| `Hemispheres.o` bss | 104 B | 104 B | 104 B |
| `Hemispheres.o` total | 13361 B | 14361 B | 19230 B |
| `Hemispheres.o` on disk | ~53 KB | ~75 KB | ~100 KB |

`kMaxAppletSize` is now dominated by GateDelay's 2x64 `uint32_t` ring buffer (~540 bytes per side, ~1.1 KB across both sides). Per-slot SRAM still sits well under 2 KB. NT plug-in flash easily absorbs the ~100 KB binary; no concern at the next several Tier 3 ports either.

### Notes and limitations

- `Modulate<T>` stub drops the vendor's `SemitoneIn` quantizer branch. The shim has no `SemitoneIn` plumbing. Benign for Brancher, which only exercises the `max=100` integer-CV path. A future quantizer-using applet will need the branch restored.
- 5-arg `gfxDottedLine(x, y, x2, y2, density)` approximates vendor dash semantics: `density <= 1` maps to `0xFF` (near-solid), `density == 2` to `0xAA` (sparse), else `0x88` (sparser). Close enough that TLNeuron's bias and threshold indicators read correctly; exact spacing differs from O_C hardware.
- `ClockCycleTicks(ch)` returns the raw per-channel cycle ticks from `HS::frame`. The shim has no clock-multiplier subsystem, so ClkToGate's multiplied modes effectively run at base clock rate. Acceptable for the divide modes that exercise the bulk of the applet.
- 2-arg `gfxPrint(int x_adv, int num)` pads spaces by `x_adv / 6` then prints `num`. Matches vendor right-pad semantics. Does not conflict with the existing 3-arg position-and-print overload because the argument types differ.

### Files touched in Round 7

```
applets/Hemispheres.cpp
shim/include/Arduino.h
shim/include/HSicons.h
shim/include/HSUtils.h
shim/include/HemisphereApplet.h
shim/include/HemispheresFactory.h
shim/include/PhzIcons.h
shim/src/icons.cpp
harness/scripts/probe_applet.sh
```

No new shim files; the harness script is the only addition outside the shim tree.

## Observations

- The C++11 `constrain` polymorphism issue is recurring. Three argument types make it brittle; consider a non-template Arduino-style macro if more applets hit this.
- Icons accumulate steadily (3 → 9 → 10 over four applets). Audit feasible: a `HS_ICON_TABLE` generated from a list of names would replace the manual declaration + definition pairs.
- `PhzIcons::*` is a stable extension point. Each applet just needs one new placeholder. Could autogenerate from a list, or accept that adding a new placeholder is the marginal cost of a new applet.
- No applet so far has required `gfxHeader`, `gfxBitmap` with non-8 heights, or any Phazerville `HSApplication`-derived behavior. If a Tier 2 applet needs these, the shim grows again.

### TODO: NT-wide quadrants mode (4 applets per slot)

NT screen is 256x64, twice the width of the O_C 128x64 that Phazerville targets. Hemispheres uses two halves (left 0..127, right 128..255). The remaining width opens up a "quadrants" variant: 4 applets per slot at 64 px each (matching O_C native applet width).

Sketch:

- `QuadrantsShim` mirroring `HemispheresShim` with four selectors (Slot A/B/C/D) and 32 routing params (gate + CV + out + mode per side x 4).
- `gfx_offset` advances 0 -> 64 -> 128 -> 192 between `View()` calls. `channel_offset()` becomes `hemisphere * 2` for sides 0..3 (channels A/B/C/D/E/F/G/H mapped via NT inputs 1..8 if scaled, or constrained to first 4 channels).
- I/O budget: 4 applets x 2 channels each = 8 in + 8 out. Bare NT has 12 buses total (overlap of in/out via routing); tight but doable. With an NTX (8 CV) and/or CVM expander attached, the bus count expands and quadrants comfortably has dedicated input + output buses per slot. Detect or document the expander requirement in the plug-in description.
- UX: encoder mapping unclear with 4 applets and only 2 encoders. Possible: L encoder = "active slot" cursor, R encoder = value; or hold-modifier; or per-slot button mapping using `kNT_button1..4`.
- `gfxHeader` already side-aware via `hemisphere & 1`. Generalise to `hemisphere & 3` and pick left/right alignment per slot. Or simplify: left-align all.

Out of scope for Plan F. Not blocking. Document here so the option is not forgotten.

## Files touched cumulatively

```
shim/include/Arduino.h
shim/include/HSIOFrame.h
shim/include/HSUtils.h
shim/include/HSicons.h
shim/include/HemisphereApplet.h
shim/include/HemispheresFactory.h
shim/include/CVInputMap.h
shim/include/PhzIcons.h
shim/include/hem_graphics.h
shim/include/hem_shim.h
shim/include/hem_shim_impl.h
shim/src/globals.cpp
shim/src/graphics.cpp
shim/src/icons.cpp
```

14 files. The shim is the entire NT-side adaptation; vendor source unchanged.
