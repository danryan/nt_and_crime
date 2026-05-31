# ENVGEN port design

- Vendor pin: `7800d929`. Issue: #45. Brainstorm:
  `docs/superpowers/brainstorms/2026-05-30-ENVGEN-brainstorm.md`.
- GUID: `OCEG`. Output: gate-triggered unipolar 4-channel modulation envelopes.

## Canonical recipe reference

Quad-channel OC::App port. Follows `plugins/apps/BBGEN.cpp` verbatim in structure
(file-scope `QuadEnvelopeGenerator envgen` singleton, the `SettingsFacade` quad
facade dispatching `idx/ENV_SETTING_LAST` to `envelopes_[ch]` setting
`idx%ENV_SETTING_LAST`, `construct_with_facade`, channel-prefixed names,
`dispatch_custom_ui_factory<false>` because the vendor reads only
`EVENT_BUTTON_PRESS`).

## Deliverables

### 1. kMaxSettings 80 -> 160 (cross-cutting, user-approved)

ENVGEN flattens 4 x 33 = 132 NT rows. `kMaxSettings` raised to 160 in
`_per_app_runtime.h`. Verified the raise leaves `.text`/`.data`/`.bss`
byte-identical for existing apps (H1200 text=27344, POLYLFO text=29674 unchanged);
it grows only `req.sram`. Consequence: one device reboot after deploy so shipped
apps re-cache `calculateRequirements`.

### 2. Layer 0: `menu::DrawMask` (reusable by SEQ)

Both template overloads hand-ported into shim `OC_menus.h` from vendor
`OC_menus.h:189-230` (pure `graphics.drawRect`). Lets vendor
`OC_euclidean_mask_draw.h` (`OC::EuclideanMaskDraw`) compile unmodified.
`EuclideanPattern` is already shim-owned (`shim/src/cv_map/bjorklund.cpp`).

### 3. Additive shim symbols (mechanical hand-ports)

- `OC::Strings::envelope_shapes[11]`, `reset_behaviours[5]`,
  `falling_gate_behaviours[2]`, `trigger_input_names_none[13]` (header
  `OC_strings.h` + defs `globals.cpp`; vendor `OC_strings.cpp:131,156,160,75`).
- `OC::DigitalInputDisplay` (trigger-flash animator, vendor
  `OC_digital_inputs.h:148`) into shim `OC_digital_inputs.h`; references
  `OC_CORE_ISR_FREQ`, so the shim header now includes `OC_config.h`.
- `OC_CORE_TIMER_RATE` (`= 1000000 / OC_CORE_ISR_FREQ`) into shim `OC_config.h`.
- `OC::kBitmapLoopMarkerW` + `OC::bitmap_loop_markers_8` (loop markers, vendor
  `OC_bitmaps.cpp:58`) into shim `OC_bitmaps.h` + `shim/src/oc/menus.cpp`.
- `menu::SettingsListItem::DrawValueMax` (vendor `OC_menus.h:360`) into shim
  `OC_menus.h`.

### 4. `plugins/apps/ENVGEN.cpp`

BBGEN template. `#define ENABLE_APP_PIQUED 1` (NOT `ENABLE_APP_ENVGEN`; the vendor
guard keeps the app's original "Piqued" name) before `#include "APP_ENVGEN.h"`.
Quad facade over `QuadEnvelopeGenerator::envelopes_`, `ENV_SETTING_LAST == 33`,
`numParameters = kIoParamCount + 132`. `.customUi = dispatch_custom_ui_factory<false>`.

### 5. `shim/include/oc_app_manifests/ENVGEN.h`

GUID `NT_MULTICHAR('O','C','E','G')`, name `"4x EG"`. I/O: 4 CV in, 4 CV out (Env
A-D), 4 trigger in (the channels' default DIGITAL_INPUT_1..4 gates).

### 6. Makefile

`OC_APP_LIST += ENVGEN`. `VENDOR_DEPS_ENVGEN := peaks_multistage_envelope.o
peaks_resources.o` (host srcs likewise). No bjorklund dep (shim-owned).

### 7. `harness/tests/test_oc_app_ENVGEN.cpp`

BBGEN-style: factory/registration (GUID OCEG, 132 settings, 33/channel),
channel-prefixed names (rows 0/33/66/99 prefixed A/B/C/D), draw, gate-triggered
output (raise TR in 1 -> bus 5, assert Env A on bus 13 moves and stays 0..+5V;
ungated Env C on bus 15 stays 0V), 132-setting blob round-trip, parameter-store
sync.

## Spec footer

### Recipe spot-check

BBGEN quad recipe applied: `make_quad_facade()` identical shape, `envelopes_`
substituted for `balls_`, `ENV_SETTING_LAST` (33) for `BB_SETTING_LAST` (11).
Divergences from BBGEN: kMaxSettings raise (BBGEN fit under 80); five net-new shim
symbols (DrawMask, DigitalInputDisplay, OC_CORE_TIMER_RATE, loop bitmap,
DrawValueMax) the foundation lacked; `ENABLE_APP_PIQUED` guard name.

### Per-entry verification (3 settings traced to vendor source)

1. `ENV_SETTING_TYPE` (index 0): `SETTINGS_DECLARE` row 0 =
   `{ ENV_TYPE_AD, ENV_TYPE_FIRST, ENV_TYPE_LAST-1, "TYPE", envelope_types, U8 }`
   (APP_ENVGEN.h:736). Default AD. int16-safe. Maps to quad rows 0/33/66/99.
2. `ENV_SETTING_SEG1_VALUE` (index 1): `{ 128, 0, 255, "S1", NULL, U16 }`
   (APP_ENVGEN.h:737). U16 but range 0..255, int16-safe; the round-trip test
   writes/reads it per channel. Confirmed.
3. `ENV_SETTING_AMPLITUDE` (index 29): `{ 127, 0, 127, "Amplitude", NULL, U8 }`
   (APP_ENVGEN.h:773). Default 127 (full), so a gated channel produces a non-zero
   envelope without test setup; the output test relies on this. Confirmed.

### Shim prereq verification

- `envelope_shapes`/`reset_behaviours`/`falling_gate_behaviours`/`trigger_input_names_none`:
  vendor `OC_strings.cpp:131,156,160,75`. MISSING -> deliverable 3. Confirmed.
- `DigitalInputDisplay`: vendor `OC_digital_inputs.h:148`. MISSING -> deliverable 3.
  Confirmed.
- `OC_CORE_TIMER_RATE`: vendor `OC_config.h:24`. MISSING -> deliverable 3. Confirmed.
- `bitmap_loop_markers_8`/`kBitmapLoopMarkerW`: vendor `OC_bitmaps.cpp:58`/`.h:41`.
  MISSING -> deliverable 3. Confirmed.
- `menu::DrawMask`: dropped from the foundation shim `OC_menus.h`. MISSING ->
  deliverable 2. Confirmed.
- `SettingsListItem::DrawValueMax`: vendor `OC_menus.h:360`. MISSING -> deliverable
  3. Confirmed.
- `EuclideanPattern`, `peaks_resources` LUTs, `peaks_multistage_envelope`: present
  / linked via shim cv_map + `VENDOR_DEPS_ENVGEN`. No work.
