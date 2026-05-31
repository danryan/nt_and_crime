# Brainstorm: port DQ (Dual Quantizer / Meta-Q) to an NT plug-in

Date: 2026-05-31
Status: accepted
Vendor SHA: 7800d929f25868f9a8b7d3d50514532ee001649b
Issue: #48 (part of the #36 remaining-OC-apps track)

## Scope

Port the vendor O_C app `APP_DQ.h` (the "Meta-Q" dual quantizer) to one NT
plug-in `.o` under `plugins/apps/`. Two independent quantizer channels, each with
four scale slots, a Turing-machine source, and a main + aux CV output.

## App shape

- Multi-channel facade port: the BBGEN quad-facade shape with N = 2. The vendor
  keeps the two channels in the file-scope array `dq_quantizer_channels[2]`, each
  a `DQ_QuantizerChannel` `SettingsBase` of 31 settings. The facade instance is
  the array base; lambdas index `[idx / per_channel]`.
- Output: pitch. `DQ_isr` drives channel 0 -> DAC A (main) + C (aux), channel 1
  -> DAC B (main) + D (aux). Four DAC outputs.
- customUI: the vendor `DQ_handleButtonEvent` reads `EVENT_BUTTON_LONG_PRESS`
  (long-press toggles a channel / opens the scale editor), so the dispatch flag
  is `<true>`. DQ instantiates `OC::ScaleEditor<DQ_QuantizerChannel>`, so it needs
  the PASSENCORE `OC::UI`-alias mechanism (include `ui_events.h` before the
  runtime).

## Settings model (per-channel subset, masks excluded)

31 settings/channel. Four are `STORAGE_TYPE_U16` scale masks
(`DQ_CHANNEL_SETTING_MASK1..MASK4`, range 1..65535, contiguous indices 9..12),
which overflow `int16_t` and are edited through the scale-editor customUI, so
they are excluded (the FPART/PASSENCORE U16 lesson). The remaining 27/channel are
NT parameters. The excluded block is mid-array, so the facade remaps a
within-channel logical row `w` to physical `w < 9 ? w : w + 4`. 2 * 27 = 54 flat
NT rows; `numParameters = 12 + 54 = 66`.

Persistence: the masks live in the same per-channel `SettingsBase` whose `Save()`
serialises all 31 values; `DQ_save` loops both channels' full `Save`, so the
facade blob hooks are overridden to the whole-app `DQ_save`/`DQ_restore` and the
masks survive a preset reload despite not being parameters.

## Vendor dependencies

App includes: OC_apps, util_settings, util_trigger_delay, braids_quantizer(+
scales), OC_menus, OC_visualfx (NEW), OC_scales, OC_scale_edit, OC_strings,
OC_digital_inputs, OC_ADC, extern/dspinst.h (NEW). Plus `util/util_turing.h`
(used by the channel but not self-included, so the per-app `.cpp` includes it).

- `braids_quantizer` + `OC_scales` ride the OC shim-impl aggregation. No per-app
  dep. `OC_chords`/`OC_input_map` are NOT used (unlike PASSENCORE).
- `OC_visualfx.h` (`OC::vfx::ScrollingHistory`), `util/util_history.h`,
  `util/util_turing.h`, `extern/dspinst.h` are all header-only. `dspinst.h` has
  ARM-only asm guarded by `#if __ARM_ARCH_7EM__` with a portable C `#else`
  fallback, so it compiles on the host as-is. No new poison shadows.
- `VENDOR_DEPS_DQ` is empty.

## Shim additions forecast

- `OC::Strings::{cv_input_names, channel_trigger_sources, TM_aux_cv_destinations}`
  (the scale editor / DQ settings reach them; ported into globals.cpp).
- `OCTAVES` (vendor `OC_config.h:41`), `OC::DAC::{get_octave_offset,
  pitch_to_scaled_voltage_dac}`, `menu::SettingsListItem::Draw_PW_Value`.
- The `OC::UI` alias mechanism and the full `scale_names` table already landed
  with PASSENCORE.
- `OC_scale_edit.h` and its symbols (Strings tables, UiOps no-ops, DAC::set_scaling,
  bitmaps) already landed with PASSENCORE.

## Out of scope

- The exact main-channel output pitch on the host: the vendor routes the
  quantizer result through `pitch_to_scaled_voltage_dac` with a continuous-mode
  offset against a calibrated DAC; the shim collapses to an uncalibrated 1V/oct
  model, so host pitch fidelity is not asserted (the audio path running is). Pitch
  fidelity is a hardware-smoke concern.
- Hardware ADD smoke check (GUID `OCDQ`) deferred to the user.

## Abort conditions

- More than 2 unforeseen poison shadows: halt.
- Any U16+ setting forced into an int16 param: halt.
- `.text` over the ~82 KB cap: halt (not expected; template apps are ~30 KB).
