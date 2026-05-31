# Design: SEQ (Sequins) NT plug-in

Date: 2026-05-31
Status: implemented
Vendor SHA: 7800d929f25868f9a8b7d3d50514532ee001649b
Issue: #52

## Recipe

Multi-channel OC-app port, DQ template (N = 2) with a contiguous per-channel U16
subset and a whole-app blob override. Two vendor UI editors.

- `plugins/apps/SEQ.cpp`: `NT_OC_APP_TU`; `ui_events.h` FIRST (both editors do
  member access on `UI::Event` inside namespace OC -> the OC::UI alias);
  `#include "util/util_math.h"` before the vendor header (SlewedValue, ARM does
  not force-include); `MENU_REDRAW`; `ENABLE_APP_SEQUINS`; the `OC::App` from the
  `SEQ_*` thunks; `make_dual_facade()` (instance = `seq_channel` base) with
  get/apply/value_attr remapping a within-channel row w to physical
  `w < 10 ? w : w + 5`, and save/restore/storage_size overridden to the whole-app
  `SEQ_save`/`SEQ_restore`/`SEQ_storageSize`; channel-prefixed parameter names
  built into a file-scope `char[106][16]`; `dispatch_custom_ui_factory<true>`.
  GUID `OCSQ`.
- `shim/include/oc_app_manifests/SEQ.h`: 4 CV in, 4 CV out (Ch1/Ch2 pitch, Ch1/Ch2
  aux), 4 trig in (Ch1 clock / TR2 / Ch2 clock / TR4). I/O shape identical to DQ.
- `harness/tests/test_oc_app_SEQ.cpp`: 7 cases (factory/registration, subset
  remap, draw, trigger-driven output-smoke, settings+mask round-trip, param-sync
  both directions, offset push-back).

## Settings model

`SEQ_CHANNEL_SETTING_LAST == 58`. The five U16 masks at contiguous indices 10..14
(`SCALE_MASK` + `MASK1..MASK4`) are excluded; 53 exposed rows per channel, 106
flat NT rows. `numParameters = kIoParamCount + 106 = 118`. `kMaxSettings` (208)
already covers 106 (no raise). The masks persist through the whole-app
`SEQ_save`/`SEQ_restore` blob (each channel's `Save()` serialises all 58 values,
masks included; `SEQ_save` loops both channels). The blob is far under
`kMaxBlobBytes = 512`.

## Shim additions

- Include-guard poison `OC_UI_H_` in the shim `OC_ui.h`: APP_SEQ.h quote-includes
  the vendor `OC_ui.h` (same-dir), which hard-includes the non-portable UI chain
  (OC_config / OC_gpio / OC_debug / UI/ui_button / UI/ui_encoder / util_profiling)
  and redefines UiControl / CONTROL_BUTTON_*. Defining the vendor guard makes the
  vendor body self-suppress; the shim `OC_ui.h` already provides UiControl, the
  CONTROL_BUTTON_* aliases, and the OC::ui no-op object. Same technique as
  OC_menus.h / OC_config.h / OC_strings.h.
- `OC::Strings::seq_playmodes` (vendor `OC_strings.cpp:53`, 16 labels " -" .. "CV#4"):
  decl in shim `OC_strings.h`, def in `globals.cpp`.
- `OC::ADC::smoothed_raw_value(ADC_CHANNEL)` (vendor `OC_ADC.h:72`): collapses to
  `raw_value` (no separate smoothing pipeline in the shim).
- `menu::SettingsListItem::Draw_PW_Value_Char` (vendor `OC_menus.h:316`): identical
  to the existing `Draw_PW_Value` except the `DrawCharName(name_string)` lead-in.
- `weegfx::Graphics::print(int, unsigned width)` (vendor
  `src/drivers/weegfx.cpp:512`): right-justify the integer in a `width`-char field,
  left-padded with spaces. Decl in `hem_graphics.h`, def in `graphics.cpp`.
- `VENDOR_DEPS_SEQ` = `OC_patterns.o` (pattern storage + `pattern_names_short`),
  `OC_input_map.o` (the S+H / CV-playmode input maps), and the peaks
  multistage-envelope DSP pair `peaks_multistage_envelope.o` + `peaks_resources.o`
  (the aux output, same as ENVGEN). braids_quantizer + OC_scales ride the OC
  shim-impl aggregation; the pattern-editor, scale-editor, input-maps, arp,
  trigger-delay, and dspinst headers are header-only and portable as-is.

## Verification footer

- Recipe spot-check: built host (7/7, 103 assertions) and ARM (`.text` 48190 B,
  `nm` clean: only firmware-provided symbols unresolved).
- Per-entry verification (3 traced against `APP_SEQ.h`):
  - `SEQ_CHANNEL_SETTING_SCALE_MASK` index = 10 (enum line 147),
    `STORAGE_TYPE_U16` range 1..65535 (`SETTINGS_DECLARE` line 2001). Matches:
    first excluded mask, remap start = 10.
  - The mask block is five contiguous U16 entries: SCALE_MASK (10), MASK1 (11),
    MASK2 (12), MASK3 (13), MASK4 (14), all `STORAGE_TYPE_U16` (lines 2001,
    2003-2006). Matches: `kNumMasks = 5`, single skip block.
  - `SEQ_CHANNEL_SETTING_LAST` = 58 (enum SEQ_CHANNEL_SETTING_MODE .. LAST, lines
    134-200). Matches: 53 exposed per channel, 106 flat rows, `numParameters`
    118.
- Shim prereq verification: `seq_playmodes`, the `OC_UI_H_` poison,
  `smoothed_raw_value`, `Draw_PW_Value_Char`, and `print(int, unsigned)` are the
  complete first-compile gap set (`g++ -ferror-limit=0 -fsyntax-only`); the
  vendor-OC_ui.h chain errors (DAC_CHANNEL_COUNT / UiControl / pinMode / etc.)
  were all one cascade cleared by the single poison.
