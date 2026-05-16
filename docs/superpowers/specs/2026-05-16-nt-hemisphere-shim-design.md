# Phazerville Hemisphere → disting NT Compatibility Shim — Design

Date: 2026-05-16
Status: Revision 4 — addresses revision-3 nits, surfaces deployment question

## Open questions for user (resolve before Stage A starts)

1. **NT deployment mechanism.** USB MSC, SD card, or dedicated tool? Spec assumed USB MSC; the first hardware-touching step in Stage A is to confirm this and rewire `make deploy` accordingly. No other infrastructure is built on top of the assumption.
2. **MIDI control surface for harness UI replay.** The custom-UI parity test wants encoder and button events delivered via MIDI to the NT. Which control-surface mapping does the NT actually expose? If none, the harness needs a different transport (e.g., direct USB-MIDI SysEx commands that simulate UI events). Verify in Stage A.

## Revision-4 changelog

- Plan-B link target spelled out: `_nt_hem_dump_screen()` lives in a header-only inline (`shim/include/hem_dump_helper.h`); the sed pass injects the include and a tail call into the reference plug-in's `draw()`. No separate .o.
- Font-dump throughput sanity check for the 21-pt font (95 glyphs × ~220 bytes ≈ 21 KB, ~94 SysEx chunks at 256-byte payload, ≈ 0.2 s at USB-MIDI rate). Anti-aliased 4-bit greys preserved by capturing `NT_screen` bytes verbatim.
- Output-mode macro semantics verified against `api.h` lines 287-289: `NT_PARAMETER_CV_OUTPUT_WITH_MODE(n,m,d)` expands to `NT_PARAMETER_IO(...kNT_unitCvOutput)` + `NT_PARAMETER_OUTPUT_MODE(n " mode")`, producing two distinct `_NT_parameter` entries. The 2-implicit-params claim in the parameter budget is correct.
- Deployment mechanism and MIDI control-surface protocol pulled into a top-level "Open questions for user" section so they aren't buried.

## Revision-3 changelog

- N1: Hardware screen capture's frame-cycle timing pinned. Four explicit Stage-A assumptions, each verified before the gate proceeds. Plan B (instrumented reference plug-in via Make-time source rewrite) added in case slot ordering or inter-algorithm screen clears defeat the co-load.
- N2: Sub-window gate resolution math documented (at 48 kHz, 32-frame block, N≈11 sub-windows → ~2.9 frames per sub-window, max resolvable clock rate ≈ 8 kHz; well above any musical clock).
- N3: `OC::CORE::ticks` block-boundary jitter noted (±1 ISR tick at block boundaries due to `lroundf(N)`; inaudible for control rate; DEVNOTES item).
- N4: Pitch calibration range widened from ±200 ppm to ±2000 ppm (math verified; ±200 ppm = ±1.44 cents @ 6 V, too tight against the 1-cent abort threshold).
- Bus enum citation corrected from "lines 65-72" to "lines 66-72" after re-reading `api.h` directly.

## Revision-2 changelog

- Cited upstream sources for every numeric claim (bus count, ISR period, font, CV scaling)
- Replaced "simulator is the reference for shape rasterisation" with "hardware is ground truth; simulator's rasteriser must be empirically matched to hardware"
- Replaced contradictory encoder/EditMode table with a single coherent story (shim does synthesise EditMode for the R encoder; documented why)
- Made controller cadence explicitly runtime-computed every `step()`, not a constant
- Added Tier 1 cadence-sensitivity audit (Burst, Slew, GateDelay) and the per-applet golden-master verification it requires
- Specified the hardware screen-capture mechanism (co-loaded SysEx dumper plug-in) instead of hedging
- Pinned upstream sources via submodules (not scripted clone) per brief's directive

## Source-of-truth citations

Every numeric or behavioural claim in this design traces to a specific upstream file. Listed here for auditability.

- **NT bus count**: `vendor/distingNT_API/include/distingnt/api.h` lines 66-72, enum `kNT_numInputBusses = 12, kNT_numOutputBusses = 8, kNT_numAuxBusses = 44, kNT_lastBus = 64`. Note that lines 442-443 in the same file contain a stale comment claiming "busFrames gives access to all 28 busses"; the enum is authoritative for `kNT_apiVersion13` (which line 47 documents as "Add bus count constants"). Hardware verification: Stage A's `bus_probe` plug-in enumerates the live module's accessible bus indices and confirms `kNT_lastBus = 64` empirically before any shim code is written.
- **NT screen layout**: `api.h` line 591-592, `NT_screen[128*64]`, 256×64 logical pixels, 4-bit grayscale, two pixels per byte. Comment in `api.h` does not specify nibble order; verify empirically.
- **Hemisphere CV unit**: `HSUtils.h` line 13, `ONE_OCTAVE = 12 << 7 = 1536`.
- **`HEMISPHERE_MAX_INPUT_CV`**: `HSUtils.h` line 20, expands to `6 * ONE_OCTAVE + NorthernLightModular * (4 * ONE_OCTAVE)` = 9216 on T4.1 (`NorthernLightModular` is undefined for T4.1, expands to 0).
- **Hemisphere ISR period**: `OC_config.h` line 23, `OC_CORE_ISR_FREQ = 16666U` Hz = 60.0024 µs per tick. The constant is **unconditional** in `OC_config.h` — not gated on `ARDUINO_TEENSY41` — so T4.1 inherits the same 60 µs ISR as T3.2. The Phazerville `API-notes.md` claim of 60 µs holds for T4.1.
- **Hemisphere clock-tick reference**: `HSUtils.h` line 22, `HEMISPHERE_CLOCK_TICKS = 17` (1 ms ≈ 17 ISR ticks).
- **OC font dimensions**: `weegfx.h` lines 35-36, `static constexpr coord_t kFixedFontW = 6; kFixedFontH = 8;`. Glyph table: `weegfx.cpp` line 416, `ssd1306xled_font6x8` from `extern/gfx_font_6x8.h`. Fixed 6×8 bitmap, 95 printable ASCII glyphs starting at code point 32.
- **NT screen-dump path**: no built-in `NT_dumpScreen` exists in `api.h`. Hardware capture requires a co-loaded helper plug-in that responds to inbound SysEx (`midiSysEx` callback) and emits `NT_screen` outbound via `NT_sendMidiSysEx`. See "Hardware screen capture" below.

## Goal

Let unmodified Phazerville T4.1 Hemisphere applet sources (`software/src/applets/*.h`) compile and run as disting NT C++ plug-ins. One applet, one NT plug-in. The shim is the translator. No applet source is edited.

## Two compilation targets

The same shim and applet sources must compile in two modes.

- Hardware target: `arm-none-eabi-c++ -std=c++11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -fno-rtti -fno-exceptions -Os -fPIC -Wall`. Produces one `.o` per applet, deployable to the NT module.
- Host target: same source, native host compiler (clang or g++), linked into the host simulator binary for fast iteration without hardware.

Identical preprocessor flags, identical source files. The only difference is the compiler and the simulator entry point.

## Glossary

- NT bus: one of 64 audio-rate float channels available to a plug-in's `step()`. Per `api.h` enum at `kNT_apiVersion13`: 12 input, 8 output, 44 aux. Layout: `busFrames[(bus - 1) * numFrames + i]`. (A stale comment elsewhere in `api.h` mentions "28 busses" — disregard; the enum is authoritative.)
- NT screen: `NT_screen[128*64]` bytes, 256×64 logical pixels, 4-bit grayscale, two pixels per byte, low nibble = left pixel.
- Hemisphere half-screen: 64×64 logical region used by one applet. `gfx_offset = (hemisphere & 1) * 64` shifts X.
- Hemisphere CV unit: integer pitch units, `ONE_OCTAVE = 12 << 7 = 1536` per octave (128 per semitone).
- Hemisphere ISR tick: ~60 µs target on Teensy 4.1, `HEMISPHERE_CLOCK_TICKS = 17` per millisecond.

## Architecture

```text
+-----------------------------------------------------------------------+
| applet plug-in source (one .cpp per applet, ~3 lines)                 |
|   #include "shim/hem_shim.h"                                          |
|   #include "applets/Logic.h"          // upstream, byte-for-byte      |
|   NT_HEM_PLUGIN(Logic, "Hl01", "Hemisphere Logic")                    |
+-----------------------------------+-----------------------------------+
                                    | macro expands to NT plug-in       |
                                    | boilerplate + HemisphereShim<T>   |
                                    v
+-----------------------------------------------------------------------+
| shim library (static, header + .cpp)                                  |
|   include/Arduino.h, OC_core.h, OC_DAC.h, OC_ADC.h, OC_scales.h,      |
|     util_math.h, HSIOFrame.h, HSClockManager.h, CVInputMap.h,         |
|     HSicons.h, PhzIcons.h, OC_gpio.h, OC_strings.h                    |
|   src/graphics.cpp     -- OC graphics object writing into NT_screen   |
|   src/frame.cpp        -- global HS::IOFrame, cvmap[], trigmap[]      |
|   src/clock.cpp        -- HSClockManager stub backed by tick counter  |
|   src/hem_shim.cpp     -- HemisphereShim<T>: param table, step(),     |
|                          draw(), customUi(), serialise/deserialise,   |
|                          NT factory builder                           |
+--------+----------------------------------+---------------------------+
         | Hardware target                  | Host target
         v                                  v
+------------------------+        +--------------------------------------+
| arm-none-eabi-c++      |        | native compiler + harness/           |
| -> build/arm/<a>.o     |        |   sim_nt.cpp (host implementation of |
| -> SD card / USB       |        |   NT_screen, NT_drawText, busFrames, |
| -> disting NT          |        |   NT_globals, NT_setParameterFromUi, |
+------------------------+        |   serialisation glue)                |
                                  | -> harness/build/sim                 |
                                  | scripted load: reference plug-ins +  |
                                  | shimmed applets                      |
                                  +--------------------------------------+
```

## Repository layout

```text
.
|-- README.md
|-- Makefile                 # top-level: arm + host targets
|-- vendor/                  # gitignored; populated by `make vendor`
|   |-- distingNT_API/       # pinned commit
|   `-- O_C-Phazerville/     # pinned commit, phazerville branch
|-- shim/
|   |-- include/             # compat headers presented to applet source
|   |   |-- Arduino.h
|   |   |-- OC_core.h
|   |   |-- OC_DAC.h
|   |   |-- OC_ADC.h
|   |   |-- OC_scales.h
|   |   |-- OC_strings.h
|   |   |-- OC_gpio.h
|   |   |-- OC_digital_inputs.h
|   |   |-- util/util_math.h
|   |   |-- src/drivers/FreqMeasure/OC_FreqMeasure.h
|   |   |-- bjorklund.h
|   |   |-- HSicons.h
|   |   |-- PhzIcons.h
|   |   `-- hem_shim.h       # NT_HEM_PLUGIN macro, HemisphereShim<T>
|   |-- src/
|   |   |-- graphics.cpp
|   |   |-- frame.cpp
|   |   |-- clock.cpp
|   |   |-- inputmap.cpp
|   |   |-- icons.cpp        # icon byte arrays (only those tier 1+2 use)
|   |   `-- strings.cpp      # capital_letters etc.
|   `-- README.md
|-- applets/                 # one trivial .cpp per applet
|   |-- Logic.cpp
|   |-- AttenuateOffset.cpp
|   |-- Slew.cpp
|   |-- Calculate.cpp
|   |-- Burst.cpp
|   |-- Brancher.cpp
|   |-- TLNeuron.cpp
|   `-- GateDelay.cpp
|-- harness/
|   |-- include/distingnt/
|   |   |-- api.h            # symlink or copy of vendor header
|   |   `-- serialisation.h
|   |-- src/
|   |   |-- nt_runtime.cpp   # NT_screen, NT_drawText, NT_globals impl
|   |   |-- nt_jsonstream.cpp
|   |   `-- main.cpp         # script driver
|   `-- scripts/             # YAML/JSON scripts driving sim runs
|-- tests/
|   |-- reference/           # parity captures from hardware
|   `-- golden/              # expected sim outputs
|-- docs/superpowers/specs/  # this doc lives here
`-- plans/                   # implementation plans
```

`vendor/` is **git submodules**, pinned per the brief's directive ("Pin via submodule or recorded SHA" — submodule chosen for clarity and reproducibility). `.gitmodules` records the two upstreams (`distingNT_API`, `O_C-Phazerville`); SHAs are pinned in the gitlink and surfaced in the README. `make vendor` is `git submodule update --init --recursive`.

## Translation contract

The shim must answer six questions for each applet, identically on hardware and in the sim.

### 1. Bus reads (NT → Hemisphere `frame`)

Per `step()` call, the shim populates a global `HS::IOFrame frame` with the four Hemisphere channels `(io_offset + 0, io_offset + 1, io_offset + 2, io_offset + 3)`. Only channels 0 and 1 are addressed by HemisphereApplet methods (`io_offset = 0` is forced; the shim does not emulate the dual-applet pairing). Channel data is sampled from user-mapped NT busses at the start of each `step()`. Per-sample variation within the block is collapsed to the block's mean (sufficient for CV; gates use rising-edge detection on the first sample of the block where the value crosses the gate threshold).

CV scaling NT → Hem:

```c
hem_int = clamp((int)round(nt_float * 1536.0f), -HEMISPHERE_MAX_INPUT_CV, HEMISPHERE_MAX_INPUT_CV)
```

where `1.0f` on the NT bus = 1 volt. `HEMISPHERE_MAX_INPUT_CV = 6 * ONE_OCTAVE = 9216` on Teensy 4.1.

Gate detection: `gate_high = nt_float > 1.0f` (1 V threshold). `Clock(ch)` is true once per block on a low→high transition.

### 2. Bus writes (Hemisphere `frame` → NT)

After `Controller()` returns, the shim writes the Hemisphere channels' output buffers to user-mapped NT output busses. Two output-mode params per output bus, per NT convention (add / replace), supplied by `NT_PARAMETER_CV_OUTPUT_WITH_MODE`.

CV scaling Hem → NT: `nt_float = hem_int / 1536.0f`. Gates: `GateOut(ch, true)` produces `PULSE_VOLTAGE * ONE_OCTAVE = 6 * 1536 = 9216` → 6.0 V on the NT bus. This is intentionally above the 5 V Eurorack gate convention but matches the Hemisphere source verbatim.

`ClockOut` writes the gate-high value for the requested tick count then returns low. The shim tracks remaining-high-ticks per output channel.

### 3. Controller cadence

NT `step()` is the only beat. The shim calls `applet.Controller()` `N` times per `step()`, recomputed every call (not a constant):

```c
const uint32_t numFrames    = (uint32_t)numFramesBy4 * 4u;
const float    blockSeconds = (float)numFrames / (float)NT_globals.sampleRate;
const float    ticksPerSec  = 16666.0f;  // OC_CORE_ISR_FREQ from OC_config.h:23
int N = (int)lroundf(blockSeconds * ticksPerSec);
if (N < 1) N = 1;
```

`numFramesBy4` is a runtime argument; `sampleRate` is read from `NT_globals` at construct time. Both can change across hosts, so `N` is recomputed every block.

`OC::CORE::ticks` is provided as a monotonically incrementing 32-bit counter advanced once per `Controller()` invocation. Per-tick state also updated between invocations: `frame.tick++`, `clocked[]` re-evaluated for rising edges sampled at sub-block resolution (see below), `cursor_countdown[]` decremented via `ProcessCursors()`.

**Sub-block resolution of digital edges.** Bus reads at the start of `step()` give the shim the full block of `numFrames` samples per input bus, not just the block mean. The shim divides the block into `N` sub-windows and computes a per-sub-window gate state (high if any sample in that window crosses the 1 V threshold). This gives `Clock(ch)` rising-edge fidelity at the same `~60 µs` resolution Hemisphere expects on hardware. Cost: `O(numFrames)` per gate input per block, trivial.

*Maximum resolvable clock rate.* At 48 kHz with a 32-frame block, `N≈11`, each sub-window spans ~2.9 frames. Maximum resolvable input clock rate (Nyquist of the sub-window grid) is ~8 kHz. Well above any musical clock — Phazerville's own ISR resolves clocks at the same ~16.6 kHz nominal grid. Slower hosts with larger `numFramesBy4` reduce this proportionally; document the trade-off.

**Block-boundary tick jitter.** `N = lroundf(blockSeconds * 16666)` rounds to an integer; the true expected ticks per block has a fractional part. Across many blocks the cumulative tick count tracks wall-clock time to better than one part in N (≈ 10%), but consecutive blocks may differ by ±1 tick. Applets with internal counters (e.g., `GateDelay` measures gate-to-output delay in ticks) may exhibit ±1 ISR tick (~60 µs) of jitter at block boundaries. Inaudible for any musical use. DEVNOTES item, no mitigation needed for v1.

CV inputs are a different question. Hemisphere applets read `In(ch)` per Controller tick and expect a fresh value. Resampling CV at 16.6 kHz from a 48 kHz block is straightforward (decimation, no anti-alias filter needed for control-rate CV). The shim does this for `cvmap[].In()`, giving each `Controller()` invocation a CV sample taken from the block at the corresponding sub-window's centre frame.

This brings the cadence behaviour closer to hardware than a "block mean" approximation. Audit Tier 1/2 for any remaining divergence:

- `Logic` — gate-only, sub-window resolution sufficient.
- `AttenuateOffset` — CV-only output, per-tick resampling captures intra-block CV motion.
- `Slew` — CV smoothing; per-tick resampling gives the slew engine the same step inputs it would see on hardware. Compare golden-master at block boundaries.
- `Calculate` — arithmetic on `In()`; per-tick resampling adequate.
- `Burst` — CV modulates burst density and gates fire on `Clock(0)`. Sub-window gate resolution required; CV resampling provides density modulation parity.
- `Brancher` — gate routing, sub-window gate resolution sufficient.
- `TLNeuron` — gate + CV thresholding, both addressed.
- `GateDelay` — gate input with internal tick timer; needs sub-window gate resolution **and** internal tick counter advanced every Controller call. Already in shim contract.

Any Tier 1/2 applet that fails golden-master parity due to cadence (verified during harness validation against hardware) escalates to a per-applet fix, not a silent degrade. Future audio-rate applets (out of scope) will fail loudly: the shim does not interpolate output values between Controller calls; the last-written `frame.outputs[ch]` is replicated across the remaining frames of the block, producing visible stairstep on a scope.

### 4. Drawing (Hemisphere `graphics` → `NT_screen`)

The shim provides a global `graphics` object whose API matches the subset of `weegfx::Graphics` that HemisphereApplet uses (`setPrintPos`, `print(int)`, `print(const char*)`, `printf(...)`, `drawFrame`, `drawRect`, `invertRect`, `clearRect`, `drawLine`, `drawCircle`, `drawBitmap8`, `setPixel`, `getPrintPosX/Y`). All writes target a logical 1-bit framebuffer that the shim flushes into `NT_screen` once `draw()` finishes for the block. 1-bit pixels become `0x0` or `0xF` 4-bit values; the shim leaves untouched bytes outside the applet's screen region.

Screen region is configurable per plug-in instance via two NT params: `Screen X` (0..192 in pixels, step 4) and `Screen Y` (0..0, locked to 0 in v1 since NT is exactly 64 px tall). Default `Screen X = 0`. The shim translates Hemisphere's `(x + gfx_offset, y)` writes into `(x + Screen X, y + Screen Y)` on the NT screen, with `gfx_offset` forced to 0 (single applet, left half).

Font: ship a 6×8 bitmap font matching the OC `graphics.print` glyph metrics. Hemisphere code assumes 6 px advance per char and 8 px ascent. The font is embedded as a static byte table in `shim/src/font.cpp`.

### 5. UI translation (NT customUi → Hemisphere encoder/button)

`hasCustomUi()` returns `kNT_encoderL | kNT_encoderR | kNT_encoderButtonL | kNT_encoderButtonR | kNT_button3 | kNT_button4`. The shim is the sole consumer of those controls; the host's default behaviour for them is suppressed.

**Hemisphere's native UI model.** An applet has one logical encoder. Reading `EditMode()` selects behaviour: false = move cursor between fields, true = change the field's value. The button toggles `EditMode`. The applet itself owns the `EditMode` flag (it lives in `enc_edit[hemisphere].isEditing` and the applet's `CursorToggle()` mutates it).

**The shim's two-encoder mapping.** NT has two encoders and the user expects them to do different things. The shim cannot meaningfully give one encoder "cursor" and the other "value" without forcing `EditMode` for the value encoder, because the applet's `OnEncoderMove` itself branches on `EditMode()`. So the shim **does** synthesise `EditMode` around the right encoder: it temporarily flips `enc_edit[hemisphere].isEditing = true` before calling `OnEncoderMove(direction)`, then restores the previous value. This is the explicit single source of truth for EditMode synthesis. The applet sees a transient EditMode flip during the call but cannot observe the timing because it executes synchronously.

| NT control       | Action                                                                                                       |
|------------------|--------------------------------------------------------------------------------------------------------------|
| Encoder L turn   | call `OnEncoderMove(direction)` with the applet's current `EditMode` (cursor motion when not editing)         |
| Encoder R turn   | save `isEditing`, set `isEditing = true`, call `OnEncoderMove(direction)`, restore `isEditing`                |
| Encoder L click  | call `OnButtonPress()` (Hemisphere's primary button; default toggles `EditMode`)                              |
| Encoder R click  | call `AuxButton()` (Hemisphere's secondary button; default cancels edit)                                      |
| Button 3         | spare, reserved for "help" overlay                                                                            |
| Button 4         | spare, reserved for "reset"                                                                                   |
| Pots, button 1/2 | not consumed; pass through to host                                                                            |

**Behaviour with this mapping.** A user sees: turn L to move cursor; click L to enter edit mode and L now changes values; turn R **always** changes values without needing to enter edit mode first; click R to cancel edit. Two valid editing flows: persistent (click L, turn L, click L to commit) or transient (turn R directly). Both produce identical applet state because both end with the same `OnEncoderMove(direction)` invocations.

**Side effect to note.** Drawing code that reads `EditMode()` inside `View()` will see the user's persistent state (set by L clicks), not the transient R-encoder flip, because the flip happens entirely inside `customUi()` not `draw()`. No drawing artefact. The transient flip is invisible to `View()`.

Each plug-in's `description` string documents this mapping verbatim so the disting NT's algorithm-info screen surfaces it.

### 6. Persistence (Hemisphere `OnDataRequest/Receive` ↔ NT serialise/deserialise)

`uint64_t` per applet, split as two `int32_t` JSON numbers (`"hi"` and `"lo"`), plus member name `"hem"`. Storing as one `addNumber` is unsafe because JSON numbers are float64 and lose precision above 2^53.

Bus-mapping params and screen-position params are saved by the host through the standard parameter mechanism; the shim does not touch them in `serialise`.

## NT plug-in parameter layout

Fixed per applet (all bus indices use the `NT_PARAMETER_*_INPUT/OUTPUT` macros so the host recognises them as routing).

```c
Routing page:
  CV In 1          NT_PARAMETER_CV_INPUT             ("CV In 1",   1, 1)
  CV In 2          NT_PARAMETER_CV_INPUT             ("CV In 2",   1, 2)
  Gate In 1        NT_PARAMETER_CV_INPUT             ("Gate In 1", 0, 3)
  Gate In 2        NT_PARAMETER_CV_INPUT             ("Gate In 2", 0, 4)
  CV Out 1         NT_PARAMETER_CV_OUTPUT_WITH_MODE  ("CV Out 1",  0, 15)
  CV Out 2         NT_PARAMETER_CV_OUTPUT_WITH_MODE  ("CV Out 2",  0, 16)

Display page:
  Screen X         min 0, max 192, def 0
  Screen Y         locked to 0 (single-row constraint; NT screen is 64 px tall, Hem applet is 64 px tall)

Pitch calibration page (A3 mitigation):
  Pitch scale      min -2000, max 2000, def 0   (parts-per-million adjustment to 1/1536 scaler)
  Pitch offset     min -1000, max 1000, def 0   (millivolts added after scaling)
```

**Pitch calibration range justification.** A `+200` ppm scale change applied to a 6 V output drifts the output by `6 V × 200e-6 = 1.2 mV`, which at 1 V/oct = 1.44 cents. The brief's A3 threshold is `>1 cent error`, so ±200 ppm covers the threshold with almost no margin. Widening to ±2000 ppm covers ±14.4 cents at 6 V — comfortable margin against worst-case DAC trim drift in either direction. Offset range ±1000 mV (≈ ±1200 cents at unity scaling) is wider than any realistic DC trim needs but the parameter is cheap.

Math sanity check:

```text
hem_int  = 9216           # intended 6 V output
scaler   = 1536 * (1 + ppm/1e6)
nt_float = hem_int / scaler + offset_mV / 1000
         = 6.0 / (1 + ppm/1e6) + offset_volts

@ ppm =   +200, offset = 0:  nt_float = 5.99880 V  → -1.44 cents @ 6 V
@ ppm =  +2000, offset = 0:  nt_float = 5.98802 V  → -14.4 cents @ 6 V
@ ppm = -2000, offset = 0:  nt_float = 6.01200 V  → +14.4 cents @ 6 V
```

**Bus index "0 allowed = unmapped"** semantics. Verifying: the NT firmware treats bus index 0 as "no bus" for any parameter whose `unit` is `kNT_unitCvInput`, `kNT_unitCvOutput`, etc. (these are bus selectors). When `min = 0` in the `NT_PARAMETER_*_INPUT` macro, the firmware renders the value as "None" or similar. Confirmed by inspection of how `gainCustomUI.cpp` uses `pThis->v[kParamInput] - 1`: it does the `-1` because bus 1 is the first real bus and bus 0 means unmapped; the convention is therefore stable. The shim treats `v[bus_param] == 0` as "input absent" and returns 0 from `In(ch)` / `Gate(ch)` for that channel, without ever reading `busFrames` at a negative offset.

`In(0)` reads from the CV In 1 bus; `In(1)` reads from CV In 2; `Gate(0)` and `Gate(1)` read from the gate-typed bus parameters. Hemisphere does not distinguish "digital" from "CV" in its API beyond which method (`Gate` vs `In`) is called.

Total: 10 routing/calibration params plus 2 implicit output-mode params from the `_WITH_MODE` macros. 12 total. Well below NT's per-algorithm budget.

## Per-applet plug-in source

A complete plug-in `.cpp` is three lines:

```cpp
#include "hem_shim.h"
#include "Logic.h"   // upstream applet, copied verbatim into applets/include via the Makefile vendor step
NT_HEM_PLUGIN(Logic, NT_MULTICHAR('H','l','0','1'), "Hemisphere Logic", "Phazerville Logic applet")
```

`NT_HEM_PLUGIN(klass, guid, name, desc)` expands to:

- A static `parameters[]` array and `parameterPages` (the standard layout above).
- A static `_NT_factory factory = { ... }` whose function pointers thunk to `HemisphereShim<klass>::step`, `::draw`, etc.
- A `pluginEntry()` returning the factory.

`HemisphereShim<T>` is a templated class in `hem_shim.h` that holds an instance of `T` (the applet class), plus the shared global state the applet base class needs. Per-instance state of the shim is stored in NT SRAM via `calculateRequirements`.

## Globals strategy

Phazerville's `HemisphereApplet` uses static and namespace-scoped globals: `frame`, `cvmap[]`, `trigmap[]`, `input_quant[]`, `q_engine[]`, `clock_m`, `cursor_countdown[]`, `enc_edit[]`, `cursor_start_x/y`, `help[]`, etc. The shim defines these as real globals in `shim/src/frame.cpp` and `shim/src/inputmap.cpp`. They are populated by the active `HemisphereShim<T>` instance during `step()` and `customUi()` before the applet's methods are called. This forces one-applet-per-plug-in (multiple instances of the same plug-in in the same preset would race on the globals); document that limitation in the plug-in description.

A future revision could make the globals per-instance by promoting them into the shim object and giving the applet base class an indirection layer; that would require touching the applet base, which is out of scope for v1.

## Quantizer, scales, MIDI

Tier 1 and Tier 2 applets do not call quantizer or scale APIs (verified by reading each applet header). The shim provides empty/no-op implementations of `QuantEngine`, `OC::Scales`, `braids::Quantizer`, etc., sufficient for the compilation unit to link but not to execute. Any applet outside Tier 1/2 that calls these will compile but produce silence on Out — explicit no-op, not silent corruption. Document and abort if a smoke-test applet hits these paths.

## Host simulator

The simulator implements the NT API surface that plug-ins call. Specifically:

- `NT_screen[128*64]` is a real array in BSS.
- `NT_drawText`, `NT_drawShapeI`: deterministic pixel implementations. `NT_drawText` glyphs are loaded from a captured-from-hardware font table (see "Font table capture" below); the simulator does not invent glyph shapes.
- `NT_drawShapeF` (antialiased): the simulator's rasteriser is a placeholder until hardware ground truth is captured. Hardware is the reference; the simulator must match. The harness-validation step (below) does an empirical screen diff and the simulator's rasteriser is **adjusted to match hardware**, not the other way round. If hardware's algorithm is opaque, the simulator's rasteriser is derived by fitting against captured screens for a battery of test inputs (lines/boxes/circles at sub-pixel positions).
- `NT_globals`: synthesised with `sampleRate = 48000`, `maxFramesPerStep = 64`, and a 64 KB `workBuffer`.
- `_NT_jsonStream` and `_NT_jsonParse`: in-memory JSON read/write; the harness asserts on roundtrip equality.
- `NT_setParameterFromUi` / `NT_setParameterFromAudio` / `NT_setParameterGrayedOut` / `NT_algorithmIndex`: implemented against a single in-process algorithm slot.

The simulator entry point loads a plug-in by including its `.cpp` directly (no dynamic loading; static link). One simulator binary per scripted scenario, or one binary that dispatches by command-line argument. Scenarios are described in YAML or simple text scripts: bus input arrays, encoder/button event sequences, parameter values, and expected outputs.

Output of a scenario:

- `out_bus.bin` — raw float samples on each output bus across the scripted block.
- `out_screen.pgm` — `NT_screen` snapshot after each `draw()` invocation.
- `out_params.log` — every `parameterChanged` call with `(p, value)`.

These are byte-comparable against `tests/golden/<scenario>/`, which are themselves derived from hardware captures.

### Font table capture

The NT firmware's font is not in the public API. To populate the simulator with the real glyph table, the harness ships a one-shot capture plug-in `font_dump.cpp` that calls `NT_drawText(0, 0, "...", 15)` for every printable ASCII glyph in turn, then emits the affected `NT_screen` region as SysEx. The output is parsed into a 95-entry table per font size; the simulator's `NT_drawText` uses these tables verbatim.

**Per-font throughput.** 95 printable glyphs × per-glyph byte cost:

- 3×5 tiny: ~8 bytes packed per glyph (rounded to whole bytes at 4 bpp). 95 × 8 = 760 bytes. One outbound SysEx message.
- 6×8 normal: ~24 bytes per glyph. 95 × 24 = 2.3 KB. ~9 chunks.
- 21-pt large: glyphs are antialiased — every pixel is one of 16 grey levels in `NT_screen`'s 4-bit format. Assume 21×21 bounding box = 441 pixels = ~221 bytes packed. 95 × 221 = 21 KB. ~84 chunks at 256-byte payload, ≈ 0.2 s at USB-MIDI rate.

The large-font case is the only one that approaches SysEx-buffer limits. Stage A throughput verification (assumption 4 in "Hardware screen capture") catches that before any font is captured. If the large font cannot be dumped in a single session, dump per-glyph instead of all-at-once.

Done once per NT firmware revision and cached in `tests/reference/nt_fonts/`.

### Hardware screen capture

For the harness-validation gate (and any future regression checks), `NT_screen` is captured from hardware via a co-loaded SysEx debug plug-in `screen_dump.cpp`. The mechanism has known fragility around frame-cycle timing; the design below pins the assumptions explicitly and Stage A verifies each one before committing to the approach.

**Plug-in layout:**

- Target (reference plug-in or shimmed applet) loads in algorithm slot 1.
- `screen_dump.cpp` loads in slot 2. Higher slot index, intent: NT host processes algorithms in slot order, so the dumper's callbacks fire after the target's in any given frame.

**What `screen_dump` does each frame:**

- `step()`: no-op.
- `draw()`: copies `NT_screen[0..8191]` into a static internal buffer `g_capture[8192]`. Then re-blits `g_capture` back into `NT_screen` so a co-located human can visually confirm the capture (no-op write if nothing has changed).
- `midiSysEx(const uint8_t* msg, uint32_t count)`: matches a "dump-now" command (manufacturer ID + opcode). On match, emits `g_capture` over `NT_sendMidiSysEx(kNT_destinationUSB, ...)` chunked at 256 payload bytes per outbound message with a sequence number. Harness reassembles. Inbound SysEx is the trigger; nothing else mutates `g_capture`.

**Pinned assumptions, each verified in Stage A:**

1. *Slot order draw ordering.* NT calls `slot1.draw()` then `slot2.draw()` within one frame. **Verify:** target draws a known checkerboard; dumper captures and emits; harness checks pattern equals the target's pattern, not zeroes. If captured buffer is empty, slot order is wrong or the host clears `NT_screen` between algorithms; abort A1.
2. *Screen persistence between frames.* If the host clears `NT_screen` between slot draws but not between frames, the dumper's capture is at risk of races. **Verify:** target draws once, then idles (its `draw()` does nothing on subsequent frames); after N frames, does the dumper still capture the original pattern, or only zeroes? If only zeroes, the host clears between algorithms; fall back to Plan B (see below).
3. *MIDI SysEx round-trip integrity.* Send a 256-byte test pattern in, get the same bytes back. If the NT drops SysEx or corrupts payload, abort A1.
4. *Throughput.* Capture and emit must finish before the next dump request. 8192 bytes ÷ 256-byte chunks = 32 messages. At ~1 Mbps USB-MIDI ≈ 80 ms. **Verify:** harness can request, receive, and parse a full screen in under 200 ms.

**Plan B (if assumption 1 or 2 fails):** drop the co-load approach. Instead, generate an instrumented build of each reference plug-in via a source-rewrite step. The build pipeline:

1. Copy the upstream `.cpp` to `harness/instrumented/<name>.cpp`.
2. Make-time `sed` pass injects `#include "hem_dump_helper.h"` at the top and inserts `_nt_hem_dump_screen();` immediately before the closing `}` of the `draw()` function (regex anchored to the function's signature line).
3. Compile.

`_nt_hem_dump_screen()` is a `static inline` defined in `shim/include/hem_dump_helper.h` (header-only, no separate `.o`, no link-time coupling). Its body captures `NT_screen` into a static buffer and is callable from `draw()` directly. The same header is `#include`'d by `screen_dump.cpp` and by the instrumented reference plug-ins, so there is one implementation for both the co-load and source-rewrite paths.

This is *only* used for the harness gate — shipped reference plug-ins remain byte-faithful to upstream. The "unmodified" requirement in the brief applies to *applet* sources, not to the reference plug-ins which are project test fixtures.

If Plan B also fails (the NT cannot host a plug-in that emits 8 KB of SysEx within one frame), **A1 fires**. The shim work does not begin.

## Harness-validation gate

Required by the brief; the shim is not touched until this passes.

The gate has three sub-stages.

### Stage A — Hardware capture infrastructure

Before validating any reference plug-in:

1. Build and deploy `screen_dump.cpp`. Verify SysEx round-trip identity (send a 256-byte pattern in, get the same 256 bytes back). If this fails, **abort A1 immediately**; the rest of the gate is unreachable.
2. Build and deploy `font_dump.cpp`. Capture the 6×8, 3×5, and 21-pt font glyph tables. Store under `tests/reference/nt_fonts/`. Re-use these in the simulator.
3. Build and deploy `bus_probe.cpp` (a tiny plug-in that writes a known signal to one bus and reads back). Use it to enumerate the actual accessible bus count on the live NT and confirm the `kNT_lastBus = 64` claim from `api.h`.

### Stage B — Reference plug-in parity (`gainCustomUI.cpp`)

For each scripted scenario (`tests/scenarios/gainCustomUI/*.yaml`):

- Audio: feed a known float pattern into the configured input bus from an audio interface. Record the output bus return loopback. Compare bit-faithfully against the simulator's `out_bus.bin`. Tolerance: ±1 LSB at the converter's 24-bit resolution to accommodate the DAC's integer quantisation.
- Screen: drive the plug-in's `draw()` with the scenario's parameter values. Capture `NT_screen` from hardware via `screen_dump`. Capture from simulator via direct read. Compare byte-for-byte. **Tolerance: zero.** If hardware uses an antialiased rasteriser the simulator doesn't yet reproduce, the simulator's rasteriser is the thing that gets fixed (not the gate).
- Custom UI: replay the scenario's encoder/button event sequence into hardware via NT MIDI control-surface mapping. Capture every `parameterChanged` call by polling the relevant parameter values via SysEx between events. Compare the resulting `(p, value)` sequence and final state against the simulator's `out_params.log`. **Tolerance: zero.**

Hardware is the reference. The simulator must match. If hardware and simulator diverge:

1. Form a specific hypothesis about which simulator component is wrong (drawText glyph, drawShapeF rasteriser, bus indexing, custom UI delivery, etc.).
2. Make the targeted simulator change.
3. Re-run all scenarios.

Three such cycles per the brief's A1. If the third cycle still diverges, **abort A1**.

### Stage C — Reference plug-in parity (`gain.cpp`)

Same as Stage B but for `gain.cpp`. Confirms the audio-path validation is independent of the custom-UI path.

This entire validation is the contents of milestone "Harness-validation gate" in the plan. The blocking nature is enforced by the task dependency graph; shim core is blocked on it.

## Build

Top-level Makefile.

- `make vendor` — clone upstream repos at pinned SHAs into `vendor/`.
- `make arm` — build all applets as `build/arm/<applet>.o` for the NT module.
- `make host` — build the harness binary as `build/host/sim`.
- `make test` — run scripted scenarios for the reference plug-ins and the smoke-test applets; diff against `tests/golden/`.
- `make deploy DEVICE=/Volumes/NT` — copy `build/arm/*.o` to the NT (mounted as MSC over USB, path supplied by the user).

Compiler flags are the brief's flags verbatim for `arm`. Host build uses `-std=c++11 -fno-rtti -fno-exceptions -Wall -O2`, no Cortex-specific flags. Both targets pre-define `NT_HEM_HOST_SIM=1` or `NT_HEM_HARDWARE=1` so the shim can branch on calibration constants where the converter's true behaviour differs from the simulator (e.g. LSB quantisation).

## Risks and abort points

**A1 (harness parity unreachable).** Two failure modes:

- *Screen capture infrastructure failure.* Verified up front in Stage A; if `screen_dump` cannot round-trip SysEx, abort immediately. This is the single biggest project risk: every other validation downstream depends on it.
- *Rasteriser/font cannot be reproduced.* The simulator's `NT_drawText` is byte-faithful by construction (uses the captured font table). `NT_drawShapeF` is the open risk; if its antialiasing algorithm cannot be reverse-engineered to byte-faithfulness within three iteration cycles, abort. Photo-based fallbacks are not entertained; the brief was prescriptive about bit-faithfulness.

**A2 (Tier 1 applet needs source edits).** The shim's failure mode. Re-read the offending applet, identify the missing API surface, expand the shim. If the missing surface is in the out-of-scope set (quantizer, MIDI, audio DSP, full-screen apps), abort and report.

**A3 (CV-volt scaling cannot reconcile).** Mitigated by exposing the per-plug-in pitch calibration parameters (see "NT plug-in parameter layout"). Verify on hardware with a tuner against an applet output of `ONE_OCTAVE * n` for n = 0..5. If even with calibration the tuner shows >1 cent error across the range, A3 fires.

**A4 (time budget).** Tracked via the task list.

## Non-goals

Verbatim from the brief: no quantizer/scales/MIDI/audio-DSP applets, no `HSApplication` full-screen apps, no multi-applet composition in a single NT slot, no Hemisphere's own preset/snapshot system.

## Open questions (also listed at top)

See "Open questions for user" near the top of this document. Pulled out there so they aren't buried under implementation detail. Brief recap:

1. NT deployment mechanism (USB MSC assumed; verify in Stage A).
2. MIDI control-surface mapping for harness UI event replay (transport for encoder/button events; verify in Stage A).
