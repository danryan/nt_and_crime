# POLYLFO port design

- Vendor pin: `7800d929`. Issue: #44. Brainstorm:
  `docs/superpowers/brainstorms/2026-05-30-POLYLFO-brainstorm.md`.
- GUID: `OCPL`. Output: full-scale bipolar 4-channel modulation.

## Canonical recipe reference

Single-instance OC::App port. Follows `plugins/apps/Harrington1200.cpp` verbatim in
structure (file-scope `SettingsBase` singleton, `oc_runtime::construct` single-arg
path, `reinterpret_cast` event bridge, `dispatch_custom_ui_factory<true>` because
the vendor reads `EVENT_BUTTON_LONG_PRESS`). Foundation:
`docs/superpowers/specs/2026-05-27-oc-apps-foundation-design.md`.

## Deliverables

### 1. Shim addition: `OC::Strings::off_on`

- `shim/include/OC_strings.h`: add `extern const char* const off_on[];` in the
  `OC::Strings` namespace (next to `trigger_input_names`).
- `shim/src/globals.cpp`: define `const char* const OC::Strings::off_on[] = { "off", "on" };`
  mirroring vendor `OC_strings.cpp:117`.

### 2. Shim addition: `OC::bitmap_indicator_4x8`

- `shim/include/OC_bitmaps.h`: add `extern const uint8_t bitmap_indicator_4x8[];`.
- `shim/src/icons.cpp`: define the 4-byte array, bytes copied verbatim from vendor
  `OC_bitmaps.cpp:36`. (Read the vendor bytes at implementation time.)

### 3. Shim addition: `digitalReadFast(TR4)`

- `shim/include/OC_gpio.h`: add a `TR4` constant and a `digitalReadFast` inline.
- Semantics: vendor `digitalReadFast(TR4)` reads the raw TR4 gate; the NT analog is
  the immediate state of digital input 4. Implementation:

  ```cpp
  enum { TR1 = 1, TR2 = 2, TR3 = 3, TR4 = 4 };
  bool oc_shim_digital_read_immediate(int input_1based);  // fwd decl
  inline bool digitalReadFast(int pin) { return oc_shim_digital_read_immediate(pin); }
  ```

  Define `oc_shim_digital_read_immediate` in `shim/src/globals.cpp` (where
  `OC::DigitalInputs` is fully in scope) as
  `return OC::DigitalInputs::read_immediate(static_cast<OC::DigitalInput>(input_1based - 1));`.
  The forward-declaration + out-of-line definition avoids an `OC_gpio.h` ->
  `OC_digital_inputs.h` include cycle (`OC_gpio.h` is pulled early in the chain).

  If first compile shows `OC_gpio.h` is not in the POLYLFO include chain, include it
  from the POLYLFO `.cpp` before `APP_POLYLFO.h`. (APP_POLYLFO.h pulls TR4 via the
  vendor digital-inputs/gpio chain on hardware; the shim chain may differ. Resolve
  at first compile; either path is additive and edits no vendor source.)

### 4. `OC_options.h` resolution

`VBiasManager.h` (quote-included by `APP_POLYLFO.h`) includes `OC_options.h`. The
shim has no shadow. At first compile: if vendor `OC_options.h` (87 lines) compiles
clean with VOR undefined, do nothing. If it drags a non-portable header, add a
shim `shim/include/OC_options.h` poisoning the vendor guard `OC_OPTIONS_H_`,
providing only the macros the chain needs (and NOT defining `VOR`). Decide at first
compile. Either way, `VOR` stays undefined, so `VBiasManager` and the app's
`saveVbias`/`restoreVbias` compile to nothing and `POLYLFO_SETTING_LAST == 21`.

### 5. `plugins/apps/POLYLFO.cpp`

Mechanical from the Harrington1200 template:

- `#define NT_OC_APP_TU 1`, include `_per_app_runtime.h`, `oc_customui_dispatch.h`,
  the OC_* shim headers, `OC_gpio.h` (for TR4 if needed), `UI/ui_events.h`,
  `frames_poly_lfo.h` is pulled by `APP_POLYLFO.h`.
- `namespace menu = OC::menu;`.
- `#define ENABLE_APP_POLYLFO 1` then `#include "APP_POLYLFO.h"`.
- `struct PolyLfoInstance : public oc_runtime::AppAlgorithm {};`.
- Build `const OC::App the_polylfo_app` from the `POLYLFO_*` thunks in OC::App field
  order; `reinterpret_cast<OcEventFn>` the two event handlers.
- `calculateRequirements_impl`: `req.numParameters = oc_runtime::kIoParamCount + POLYLFO_SETTING_LAST;`
  `req.sram = sizeof(PolyLfoInstance);`.
- `construct_impl`: placement-new, `oc_runtime::construct(*inst, &the_polylfo_app, &poly_lfo, POLYLFO_SETTING_LAST);`.
- `factory`: `_NT_factory` in api.h field order, `.tags = kNT_tagUtility`,
  `.customUi = oc_runtime::dispatch_custom_ui_factory<true>`.
- `pluginEntry`.
- Test seams: `polylfo_get_setting`, `polylfo_apply_setting`, `polylfo_setting_count`
  (== POLYLFO_SETTING_LAST == 21), `polylfo_settings_param_base`, `polylfo_arm_sentinel`,
  and `polylfo_get_dac_code(int ch)` returning `poly_lfo.lfo.dac_code(ch)` for a
  direct engine assertion independent of bus routing.

### 6. `shim/include/oc_app_manifests/POLYLFO.h`

Mirror `FPART.h`. `guid = NT_MULTICHAR('O','C','P','L')`, name `"Poly LFO"`,
description naming the O_C APP_POLYLFO port. I/O block (documentation rows):

- inputs: `Freq CV`, `Shape CV`, `Spread CV`, `Mappable CV` (ADC_CHANNEL_1..4).
- outputs: `LFO A`, `LFO B`, `LFO C`, `LFO D`.
- triggers: `Reset phase`, `Freeze`, `Tempo sync`, `Freq mult` (DIGITAL_INPUT_1
  through 3 plus TR4).

### 7. Makefile

- `OC_APP_LIST += POLYLFO`.
- `VENDOR_DEPS_POLYLFO := build/arm/vendor_src/frames_poly_lfo.o build/arm/vendor_src/frames_resources.o`.
- `VENDOR_DEP_HOST_SRCS_POLYLFO := $(HEM_SRC_DIR)/frames_poly_lfo.cpp $(HEM_SRC_DIR)/frames_resources.cpp`.

### 8. `harness/tests/test_oc_app_POLYLFO.cpp`

TDD order (write before the port):

- T1 factory/registration: loads through `nt::load_plugin`, `guid == OCPL`,
  `hasCustomUi != 0`, serialise/deserialise non-null, `polylfo_setting_count() == 21`.
- T2 parameters: `numParameters == kIoParamCount + 21`; each settings row name
  matches `PolyLfo::value_attr(i).name`.
- T3 output (free-running modulation): set bus frame count; outputs A..D default to
  buses 13..16. Drive `run_steps`; assert each output moves over the run and stays
  within +-5.1V (bipolar full-scale, NOT railed). Direct check via
  `polylfo_get_dac_code` that codes are non-constant and span around the 32768
  midpoint. No bus fire-count assertions (free-running, not gated; the 10x clock
  multiplier note does not apply since output is not gated edge-counted).
- T4 round-trip: write distinct in-range values into several settings (COARSE U8,
  FINE I16, SHAPE U8, B_FREQ_DIV U8), serialise, clobber, deserialise, assert
  restored. Confirms the 21-setting blob round-trips.

## Spec footer

### Recipe spot-check

The Harrington1200 recipe applied to POLYLFO: file-scope singleton `poly_lfo` ->
facade target (matches H1200 `h1200_settings`); `dispatch_custom_ui_factory<true>`
(both read `EVENT_BUTTON_LONG_PRESS`); `numParameters = kIoParamCount + SETTING_LAST`
(matches H1200). Divergences: POLYLFO adds two vendor `.cpp` deps (H1200 had none)
and three shim symbols (H1200 reused existing). No subsetting (unlike FPART; all 21
settings int16-safe).

### Per-entry verification (3 settings traced to vendor source)

1. `POLYLFO_SETTING_COARSE` (index 0): `SETTINGS_DECLARE` row 0 =
   `{ 64, 0, 255, "C", NULL, STORAGE_TYPE_U8 }` (APP_POLYLFO.h:236). Default 64,
   range 0..255, U8. int16-safe. Maps to NT row 0. Confirmed.
2. `POLYLFO_SETTING_TAP_TEMPO` (index 2): `{ 0, 0, 1, "Tap tempo", OC::Strings::off_on, STORAGE_TYPE_U8 }`
   (APP_POLYLFO.h:238). Confirms the `off_on` dependency (deliverable 1). Enum
   string table, 2 entries. Confirmed.
3. `POLYLFO_SETTING_TR4_MULT` (index 20, the last non-VOR setting):
   `{ 3, 0, 5, "TR4: MULT", tr4_multiplier, STORAGE_TYPE_U4 }` (APP_POLYLFO.h:260).
   Default 3 (x2), range 0..5, U4. This is the LAST setting before the `#ifdef VOR`
   VBIAS row, confirming `POLYLFO_SETTING_LAST == 21` with VOR undefined. Confirmed.

### Shim prereq verification

- `off_on`: vendor `OC_strings.cpp:117` `{ "off", "on" }`. MISSING in shim ->
  deliverable 1. Confirmed.
- `bitmap_indicator_4x8`: vendor `OC_bitmaps.cpp:36`. MISSING in shim ->
  deliverable 2. Confirmed.
- `digitalReadFast` / `TR4`: shim `OC_gpio.h` is a 5-line stub, no pin macros.
  MISSING -> deliverable 3. Confirmed.
- `SCALE8_16`, `USAT16`, `CONSTRAIN`, `scope_render`, `ADC_CHANNEL_1..4`,
  `DAC_CHANNEL_A..D`, `drawBitmap8`/`printf`/`setPixel`, `menu::*`, `SmoothedValue`:
  all present (grep-confirmed). No work.
- `frames_resources.cpp`, `frames_poly_lfo.cpp`: present in vendor tree; linked via
  `VENDOR_DEPS_POLYLFO`. `extern/stmlib_utils_dsp.h`, `peaks_pattern_predictor.h`,
  `util/util_macros.h`: header-only; confirm clean compile at first build.
