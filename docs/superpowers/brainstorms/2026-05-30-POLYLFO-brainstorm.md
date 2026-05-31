# POLYLFO port brainstorm

- Vendor pin: `7800d929` (`vendor/O_C-Phazerville`, PSv1.13.1).
- Issue: #44 (4 of 12 in the OC::App per-app port program, #36).
- Status: ready to spec. Single-instance OC::App, full-scale 4-channel modulation output.
- GUID: `OCPL` (unique; shipped OC guids: OCLR, OCHA, OCSb, OCFP, OCBB, OCBT).

## What POLYLFO is

Quadrature poly LFO. Four phase-related LFO outputs (channels A-D) driven by the
Mutable Instruments Frames easter-egg wavetable engine (`frames::PolyLfo`). Output
is full-scale modulation: the engine writes raw 16-bit DAC codes via
`lfo.dac_code(0..3)`, exactly the BBGEN/BYTEBEATGEN modulation model (never the
pitch `/1536` path).

## Vendor trace (`APP_POLYLFO.h`)

- Base class: `class PolyLfo : public settings::SettingsBase<PolyLfo, POLYLFO_SETTING_LAST>`.
  Confirmed `OC::App`-style `SettingsBase`, NOT `HSApplication`.
- File-scope singleton: `PolyLfo poly_lfo;` plus a `poly_lfo_state` struct (left
  edit mode + a `menu::ScreenCursor`). Single-instance shape, so the
  `oc_runtime::construct(alg, app, &poly_lfo, num_settings)` path (NOT the quad
  facade). Template is `plugins/apps/Harrington1200.cpp` / `plugins/apps/FPART.cpp`.
- Settings count: `POLYLFO_SETTING_COARSE..POLYLFO_SETTING_TR4_MULT` = 21 settings
  (indices 0..20). `POLYLFO_SETTING_VBIAS` is `#ifdef VOR` only; the shim does NOT
  define VOR, so it is absent and `POLYLFO_SETTING_LAST == 21`. All 21 settings are
  int16-safe (max range 0..255 U8 / -128..127 I8/I16 / U4), so ALL become NT
  parameter rows. No FPART-style subsetting. `num_settings = 21`.
- Thunks (the `POLYLFO_*` statics, all present): `POLYLFO_init`, `POLYLFO_storageSize`,
  `POLYLFO_save`, `POLYLFO_restore`, `POLYLFO_handleAppEvent`, `POLYLFO_loop`,
  `POLYLFO_menu`, `POLYLFO_screensaver`, `POLYLFO_handleButtonEvent`,
  `POLYLFO_handleEncoderEvent`, `POLYLFO_isr`.
- customUI: `POLYLFO_handleButtonEvent` reads `UI::EVENT_BUTTON_LONG_PRESS`
  (APP_POLYLFO.h:499, DOWN long-press sets the phase-reset flag). So
  `.customUi = oc_runtime::dispatch_custom_ui_factory<true>`.
- ISR output writes: `OC::DAC::set<DAC_CHANNEL_A..D>(poly_lfo.lfo.dac_code(0..3))`.
  Four full-scale 16-bit codes, one per channel. Modulation app model.
- ISR inputs: `OC::DigitalInputs::clocked<DIGITAL_INPUT_1>` (reset phase),
  `read_immediate<DIGITAL_INPUT_2>` (freeze), `clocked<DIGITAL_INPUT_3>` (tempo
  sync); `OC::ADC::value<ADC_CHANNEL_1..4>` (freq/shape/spread/mappable CV);
  `digitalReadFast(TR4)` (free-run freq multiplier override).

## Vendor `.cpp` deps

`frames_poly_lfo.h` is NOT header-only: `frames_poly_lfo.cpp` defines
`PolyLfo::Init/Render/RenderPreview/FrequencyToPhaseIncrement`. It reads
`frames_resources.h` LUTs (`lut_increments_med`, `wt_lfo_waveforms`) defined in
`frames_resources.cpp`. So:

- `VENDOR_DEPS_POLYLFO := build/arm/vendor_src/frames_poly_lfo.o build/arm/vendor_src/frames_resources.o`
- `VENDOR_DEP_HOST_SRCS_POLYLFO := $(HEM_SRC_DIR)/frames_poly_lfo.cpp $(HEM_SRC_DIR)/frames_resources.cpp`

`frames_poly_lfo.cpp` also includes `extern/stmlib_utils_dsp.h` (header-only,
inline Interpolate helpers) and `frames_poly_lfo.h` includes
`peaks_pattern_predictor.h` (header-only, `<algorithm>`/`<cstdlib>`) and
`util/util_macros.h`. Confirm these compile against the shim at first build.

## Shim prereq list (additive work this port surfaces)

Per the BBGEN lesson, treat first-compile symbol errors as expected additive shim
work. Audited gaps:

1. `OC::Strings::off_on` (`{ "off", "on" }`, vendor `OC_strings.cpp:117`). Used by
   the TAP_TEMPO setting enum. Add to the shim `OC_strings.h` shadow + a definition
   in `shim/src/globals.cpp` (alongside the existing `trigger_input_names`).
2. `OC::bitmap_indicator_4x8` (vendor `OC_bitmaps.cpp:36`, a 4-byte bitmap). Drawn
   in `POLYLFO_menu` when the freq multiplier is active. Add the `extern` to the
   shim `OC_bitmaps.h` shadow + the byte array to `shim/src/icons.cpp` (mirroring
   the existing `bitmap_edit_indicators_8` etc.).
3. `digitalReadFast(TR4)`. The shim `OC_gpio.h` is currently a 5-line stub with no
   pin macros. Add a `TR4` identifier and a `digitalReadFast` inline that maps to
   the NT digital-input state. Semantics: vendor reads the raw TR4 gate pin; the NT
   equivalent is `OC::DigitalInputs::read_immediate<OC::DIGITAL_INPUT_4>()` (gate
   high -> free-run freq multiplier). Place so `OC_digital_inputs.h` is in scope, or
   forward via a thin accessor to avoid an include cycle. Resolve exact placement at
   spec time.

Already present (no work): `SCALE8_16`, `USAT16` (`util/util_math.h`), `CONSTRAIN`
(`HSUtils.h`), `OC::scope_render` (`OC_menus.h`, used by the screensaver),
`ADC_CHANNEL_1..4` (`OC_ADC.h`), `DAC_CHANNEL_A..D` (`OC_DAC.h`),
`graphics.drawBitmap8`/`printf`/`setPixel` (`hem_graphics.h`), the `menu::*` widgets
(`OC_menus.h`), `SmoothedValue` (`util/util_math.h`).

## VBiasManager (the issue's named Layer 0)

The issue forecasts "a VBiasManager stub". Trace shows it is a near-no-op: the
entire `VBiasManager` class body and the app's `saveVbias`/`restoreVbias` calls are
`#ifdef VOR`. With VOR undefined, `VBiasManager.h` reduces to its include guard plus
`#include "OC_options.h"`. The only real requirement is that
`#include "VBiasManager.h"` (quote-included from inside the vendor tree by
`APP_POLYLFO.h`) and its `#include "OC_options.h"` resolve and compile with VOR
undefined. No `VBiasManager` class stub is needed. Confirm `OC_options.h` compiles
(shim has no shadow; the vendor header is short, 87 lines) or add an include-guard
poison shadow if it drags non-portable headers. Resolve at spec time / first
compile.

## Output model

Full-scale modulation. `route_cv_output` maps the 16-bit code `(code - 32768) /
kCodesPerVolt` to +-5V. The Frames engine centers at code 32768 (0V) and swings the
quadrature waveforms full-scale. Bipolar, free-running (advances every ISR tick;
no trigger gate required, though TR1 resets phase and TR4 overrides the multiplier).

## Test focus

- Round-trip: serialise/deserialise of all 21 settings through the blob.
- Construct: `numParameters == kIoParamCount + 21`; construct-time sentinel guard.
- Output: drive the ISR, assert four channels produce full-scale (non-zero,
  bipolar) DAC codes that change over time (the LFO advances). No bus fire-count
  assertions (free-running, not gated).
- VBiasManager: the reusable learning is "VOR-conditional code compiles to nothing
  without the VOR define"; document for the later VOR-touching apps.

## Scope / aborts

- In scope: the single POLYLFO `.cpp` + manifest + 3 additive shim items + the two
  vendor `.cpp` deps + host test.
- Abort if the shim prereq list grows past ~6 items at first compile (signals a
  mis-drawn boundary, per the audit discipline), or if `frames_poly_lfo.cpp` needs
  a non-portable Teensy header that cannot be shadowed cleanly.
