# Phazerville Hemisphere → disting NT Compatibility Shim — Design

Date: 2026-05-16
Status: Draft, awaiting user review

## Goal

Let unmodified Phazerville T4.1 Hemisphere applet sources (`software/src/applets/*.h`) compile and run as disting NT C++ plug-ins. One applet, one NT plug-in. The shim is the translator. No applet source is edited.

## Two compilation targets

The same shim and applet sources must compile in two modes.

- Hardware target: `arm-none-eabi-c++ -std=c++11 -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb -fno-rtti -fno-exceptions -Os -fPIC -Wall`. Produces one `.o` per applet, deployable to the NT module.
- Host target: same source, native host compiler (clang or g++), linked into the host simulator binary for fast iteration without hardware.

Identical preprocessor flags, identical source files. The only difference is the compiler and the simulator entry point.

## Glossary

- NT bus: one of 64 audio-rate float channels available to a plug-in's `step()`. 12 audio in, 8 audio out, 44 aux. Layout: `busFrames[(bus - 1) * numFrames + i]`.
- NT screen: `NT_screen[128*64]` bytes, 256×64 logical pixels, 4-bit grayscale, two pixels per byte, low nibble = left pixel.
- Hemisphere half-screen: 64×64 logical region used by one applet. `gfx_offset = (hemisphere & 1) * 64` shifts X.
- Hemisphere CV unit: integer pitch units, `ONE_OCTAVE = 12 << 7 = 1536` per octave (128 per semitone).
- Hemisphere ISR tick: ~60 µs target on Teensy 4.1, `HEMISPHERE_CLOCK_TICKS = 17` per millisecond.

## Architecture

```
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

```
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

`vendor/` is gitignored and populated by a one-shot fetch script that clones the two upstream repos at recorded SHAs. Submodules are an alternative; defer that decision to fetch implementation. Either way, the SHAs are recorded in `vendor/PINS.txt` and surfaced in the README.

## Translation contract

The shim must answer six questions for each applet, identically on hardware and in the sim.

### 1. Bus reads (NT → Hemisphere `frame`)

Per `step()` call, the shim populates a global `HS::IOFrame frame` with the four Hemisphere channels `(io_offset + 0, io_offset + 1, io_offset + 2, io_offset + 3)`. Only channels 0 and 1 are addressed by HemisphereApplet methods (`io_offset = 0` is forced; the shim does not emulate the dual-applet pairing). Channel data is sampled from user-mapped NT busses at the start of each `step()`. Per-sample variation within the block is collapsed to the block's mean (sufficient for CV; gates use rising-edge detection on the first sample of the block where the value crosses the gate threshold).

CV scaling NT → Hem:

```
hem_int = clamp((int)round(nt_float * 1536.0f), -HEMISPHERE_MAX_INPUT_CV, HEMISPHERE_MAX_INPUT_CV)
```

where `1.0f` on the NT bus = 1 volt. `HEMISPHERE_MAX_INPUT_CV = 6 * ONE_OCTAVE = 9216` on Teensy 4.1.

Gate detection: `gate_high = nt_float > 1.0f` (1 V threshold). `Clock(ch)` is true once per block on a low→high transition.

### 2. Bus writes (Hemisphere `frame` → NT)

After `Controller()` returns, the shim writes the Hemisphere channels' output buffers to user-mapped NT output busses. Two output-mode params per output bus, per NT convention (add / replace), supplied by `NT_PARAMETER_CV_OUTPUT_WITH_MODE`.

CV scaling Hem → NT: `nt_float = hem_int / 1536.0f`. Gates: `GateOut(ch, true)` produces `PULSE_VOLTAGE * ONE_OCTAVE = 6 * 1536 = 9216` → 6.0 V on the NT bus. This is intentionally above the 5 V Eurorack gate convention but matches the Hemisphere source verbatim.

`ClockOut` writes the gate-high value for the requested tick count then returns low. The shim tracks remaining-high-ticks per output channel.

### 3. Controller cadence

NT `step()` is the only beat. The shim calls `applet.Controller()` `N` times per `step()`, where `N = round(numFramesBy4 * 4 * sampleRate_inv * 1e6 / 60.0)`. At 48 kHz with `numFramesBy4 = 8` (32 frames) this is ≈ 11 ticks. The shim does not subdivide the bus reads within a single `step()`; the Controller calls see the same `frame` snapshot through all iterations of one block, except for `frame.tick++` and `clocked[]` reset. For Tier 1/2 applets this is sufficient (none of them are sample-rate critical). Document precisely so future audio-rate applets fail honestly rather than silently smear.

`OC::CORE::ticks` is provided as a monotonically incrementing 32-bit counter, advanced once per Controller invocation.

### 4. Drawing (Hemisphere `graphics` → `NT_screen`)

The shim provides a global `graphics` object whose API matches the subset of `weegfx::Graphics` that HemisphereApplet uses (`setPrintPos`, `print(int)`, `print(const char*)`, `printf(...)`, `drawFrame`, `drawRect`, `invertRect`, `clearRect`, `drawLine`, `drawCircle`, `drawBitmap8`, `setPixel`, `getPrintPosX/Y`). All writes target a logical 1-bit framebuffer that the shim flushes into `NT_screen` once `draw()` finishes for the block. 1-bit pixels become `0x0` or `0xF` 4-bit values; the shim leaves untouched bytes outside the applet's screen region.

Screen region is configurable per plug-in instance via two NT params: `Screen X` (0..192 in pixels, step 4) and `Screen Y` (0..0, locked to 0 in v1 since NT is exactly 64 px tall). Default `Screen X = 0`. The shim translates Hemisphere's `(x + gfx_offset, y)` writes into `(x + Screen X, y + Screen Y)` on the NT screen, with `gfx_offset` forced to 0 (single applet, left half).

Font: ship a 6×8 bitmap font matching the OC `graphics.print` glyph metrics. Hemisphere code assumes 6 px advance per char and 8 px ascent. The font is embedded as a static byte table in `shim/src/font.cpp`.

### 5. UI translation (NT customUi → Hemisphere encoder/button)

`hasCustomUi()` returns `kNT_encoderL | kNT_encoderR | kNT_encoderButtonL | kNT_encoderButtonR | kNT_button3 | kNT_button4`. The shim is the sole consumer of those controls; the host's default behaviour for them is suppressed.

Mapping:

| NT control          | Hemisphere call                                  |
|---------------------|--------------------------------------------------|
| Encoder L turn      | `OnEncoderMove(direction)` with `EditMode=false` |
| Encoder R turn      | `OnEncoderMove(direction)` with `EditMode=true`  |
| Encoder L click     | `OnButtonPress()` (Hemisphere's left/cursor btn) |
| Encoder R click     | `AuxButton()` (Hemisphere's right/aux btn)       |
| Button 3            | spare, reserved for "help" overlay               |
| Button 4            | spare, reserved for "reset"                      |
| Pots, button 1/2    | not consumed; pass through to host               |

`EditMode` is toggled inside the applet via `CursorToggle()` which `OnButtonPress` already calls. The L/R encoder split is the shim's way of giving the user both navigation and editing without modal toggling. Document this explicitly in each plug-in's description string.

Hemisphere reads `EditMode()` to decide cursor vs value behaviour. The shim does not synthesise fake EditMode transitions; the applet sees real toggles from `OnButtonPress`.

### 6. Persistence (Hemisphere `OnDataRequest/Receive` ↔ NT serialise/deserialise)

`uint64_t` per applet, split as two `int32_t` JSON numbers (`"hi"` and `"lo"`), plus member name `"hem"`. Storing as one `addNumber` is unsafe because JSON numbers are float64 and lose precision above 2^53.

Bus-mapping params and screen-position params are saved by the host through the standard parameter mechanism; the shim does not touch them in `serialise`.

## NT plug-in parameter layout

Fixed per applet (all bus indices use the `NT_PARAMETER_*_INPUT/OUTPUT` macros so the host recognises them):

```
Routing page:
  CV In 1          NT_PARAMETER_CV_INPUT  ("CV In 1", 1, 1)
  CV In 2          NT_PARAMETER_CV_INPUT  ("CV In 2", 1, 2)
  Gate In 1        NT_PARAMETER_CV_INPUT  ("Gate In 1", 0, 3)   // 0 allowed = unmapped
  Gate In 2        NT_PARAMETER_CV_INPUT  ("Gate In 2", 0, 4)
  CV Out 1         NT_PARAMETER_CV_OUTPUT_WITH_MODE ("CV Out 1", 0, 15)
  CV Out 2         NT_PARAMETER_CV_OUTPUT_WITH_MODE ("CV Out 2", 0, 16)

Display page:
  Screen X         min 0, max 192, def 0, step 4
  Screen Y         min 0, max 0,   def 0

Hemisphere page:
  (no NT params; state driven by encoder/button UI)
```

`In(0)` reads CV In 1; `In(1)` reads CV In 2; `Gate(0)` and `Gate(1)` read the gate-typed inputs. Hemisphere does not distinguish "digital" from "CV" inputs in its API beyond which call is used; the params do.

Total: 8 parameters (plus 2 implicit output-mode params from the macros). Well below NT's per-algorithm budget.

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
- `NT_drawText`, `NT_drawShapeI/F`: pixel-perfect implementations using the same font and shape rules as the documented NT firmware. Where the firmware's algorithm is not public (font glyph table, antialiased shape rasteriser for `NT_drawShapeF`), the simulator's implementation is the reference and hardware parity is the gate (the harness-validation step below).
- `NT_globals`: synthesised with `sampleRate = 48000`, `maxFramesPerStep = 64`, and a 64 KB `workBuffer`.
- `_NT_jsonStream` and `_NT_jsonParse`: simple in-memory JSON read/write; the harness asserts on roundtrip equality.
- `NT_setParameterFromUi` / `NT_setParameterFromAudio` / `NT_setParameterGrayedOut` / `NT_algorithmIndex`: implemented against a single in-process algorithm slot.

The simulator entry point loads a plug-in by including its `.cpp` directly (no dynamic loading; static link). One simulator binary per scripted scenario, or one binary that dispatches by command-line argument. Scenarios are described in YAML or simple text scripts: bus input arrays, encoder/button event sequences, parameter values, and expected outputs.

Output of a scenario:

- `out_bus.bin` — raw float samples on each output bus across the scripted block.
- `out_screen.pgm` — `NT_screen` snapshot after each `draw()` invocation.
- `out_params.log` — every `parameterChanged` call with `(p, value)`.

These are byte-comparable against `tests/golden/<scenario>/`.

## Harness-validation gate

Required by the brief; the shim is not touched until this passes.

For `examples/gainCustomUI.cpp` and `examples/gain.cpp` (unmodified):

1. Drive the plug-in through the simulator with a scripted scenario. Record `out_bus.bin`, `out_screen.pgm`, `out_params.log`.
2. Drive the same plug-in on the connected NT hardware with the same scripted scenario, using `tests/reference/capture.{audio,screen,params}` as the capture protocol:
   - Audio: route a known input bus signal in from an audio interface, record the output bus return loopback. Compare bit-faithfully (NT outputs are integer-quantised at the converter; allow ±1 LSB tolerance after re-quantisation to match the host's DAC behaviour).
   - Screen: `NT_screen` is captured via a USB-side debug command issued through the NT's MIDI interface (the API includes a SysEx dump path; if not, the harness uses a photograph + automated diff against the simulator with a tolerance pass — and abort A1 escalates if photo-based diff cannot be made deterministic).
   - Params: replay the same encoder/button events through a MIDI-routed control surface mapped to the NT's buttons (NT's MIDI mappings expose pots/buttons). Capture parameter values via SysEx queries.
3. Diff. Bit-faithful where applicable, ±1 LSB or pixel where the converter or screen capture forces it. Iterate up to three cycles per the brief's A1.

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

A1 (harness parity unreachable): mitigated by writing the simulator's `NT_drawText` and `NT_drawShapeF` from the NT firmware's font and rasteriser source if available; if not, by treating screen parity as ±1 pixel after antialiased softening rather than bit-faithful, and recording that compromise as a deviation in `tests/reference/README.md` for the user's approval. If the user rejects the compromise, A1 fires.

A2 (Tier 1 applet needs source edits): the shim's failure mode. Re-read the offending applet, identify the missing API surface, expand the shim. If the missing surface is in the out-of-scope set (quantizer, MIDI, audio DSP, full-screen apps), abort and report.

A3 (CV-volt scaling cannot reconcile): mitigated by exposing a per-plug-in "Pitch calibration" parameter set (`scale × 1/1536 + offset`) defaulting to `1/1536` and offset zero; verify on hardware with a tuner against `AttenuateOffset` outputting `ONE_OCTAVE * n` for n = 0..5. If even with calibration the tuner shows >1 cent error across the range, A3 fires.

A4 (time budget): tracked via the task list.

## Non-goals

Verbatim from the brief: no quantizer/scales/MIDI/audio-DSP applets, no `HSApplication` full-screen apps, no multi-applet composition in a single NT slot, no Hemisphere's own preset/snapshot system.

## Open question for user

The deployment mechanism for the NT (SD card, USB MSC, network, dedicated tool) is "part of what to learn". This design names it `make deploy DEVICE=...` and treats it as MSC over USB by default. If the actual mechanism turns out to be SD-card-only or requires a specific tool (e.g. `nt-loader`), the `deploy` target rewires to that without changing anything else. Surface this when the harness milestone hits hardware.
