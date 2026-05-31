# Brainstorm: port QQ (Quantermain) to an NT plug-in

Date: 2026-05-31
Status: implemented (see ../specs/2026-05-31-QQ-design.md)
Vendor SHA: 7800d929f25868f9a8b7d3d50514532ee001649b
Issue: #50 (part of the #36 remaining-OC-apps track)

## App shape

The DQ (Meta-Q) quad-facade pattern at N = 4. The vendor "Quantermain" keeps four
`QuantizerChannel` instances in the file-scope array `quantizer_channels[4]` (each
a `settings::SettingsBase<QuantizerChannel, CHANNEL_SETTING_LAST>`, 51 settings)
plus `QuadQuantizer qq_state` holding the customUI state
(`OC::ScaleEditor<QuantizerChannel>`, the menu cursor, the selected channel).

- Guard: `ENABLE_APP_QUANTERMAIN` (non-mechanical, like DQ's METAQ; grepped).
- Output: pitch. `QQ_isr` runs `quantizer_channels[i].Update(triggers, DAC_CHANNEL_i)`
  for i in A..D, so each channel quantizes its routed CV/source to a 1V/oct
  output on its DAC channel.
- customUI: `QQ_handleButtonEvent` and `QQ_handleEncoderEvent` read
  `EVENT_BUTTON_LONG_PRESS` (lines 1373, 1396), so the dispatch flag is `<true>`.
  QQ instantiates a vendor `OC::ScaleEditor`, so it needs the PASSENCORE/DQ
  `OC::UI`-alias (include `ui_events.h` before the runtime).
- `QQ_loop()` is empty, so SCALE (index 0) is not clobbered and stays exposed.
- The vendor header self-forward-declares its button helpers (`QQ_topButton` etc.
  at lines 57-62), so no per-app forward decls are needed (unlike FPART).

## Settings model (per-channel subset, MASK excluded)

51 settings/channel (`CHANNEL_SETTING_SCALE` .. `CHANNEL_SETTING_OCTAVE_CONSTRAINT_LEN`).
Storage-type histogram across the 51 settings: 26 U8, 21 U4, 2 I8, 1 I16, 1 U16.
The only int16-unsafe one is the `STORAGE_TYPE_U16` mask `CHANNEL_SETTING_MASK` at
index 2 (range 1..65535); it overflows int16 and is edited via the scale editor,
so it is excluded per channel (the DQ pattern, but a single mask, not four). The
I16 "Fine" (-999..999) fits int16 and stays exposed.

- Within-channel skip: `phys = w < 2 ? w : w + 1`, `kExposedPerChannel = LAST - 1 = 50`.
- Flat NT rows: 4 * 50 = 200.
- Persistence: the masks live in the same per-channel SettingsBase, but the four
  channels live in a separate file-scope array (not one SettingsBase), so the
  facade `save`/`restore`/`storage_size` MUST point at the whole-app
  `QQ_save`/`QQ_restore`/`QQ_storageSize` (the DQ/AUTOMATONNETZ blob-override
  pattern), or only one channel persists. Those thunks serialise all four
  channels' full 51 settings each (masks included).

## kMaxSettings raise (the one shared-surface change)

200 flat rows exceeds the current `kMaxSettings = 160` (raised for ENVGEN's 132).
Raise to 208 in `plugins/apps/_per_app_runtime.h`. Per the ENVGEN lesson this only
enlarges the per-instance `parameters_storage`/`v_storage` arrays (DATA in
`req.sram`), never `.text`; the on-device consequence is the documented SRAM-cache
reboot (deploy, then power-cycle once so previously-scanned apps re-cache
`calculateRequirements`).

## Vendor dependencies

Includes: util_logistic_map (NEW), util_settings, util_trigger_delay, util_turing,
util_integer_sequences, util_math, peaks_bytebeat (has `.cpp`), braids_quantizer,
braids_quantizer_scales, OC_menus, OC_visualfx, OC_scales, OC_scale_edit,
OC_strings, HSIOFrame (see risk below).

- `braids_quantizer` + `OC_scales` ride the OC aggregation. `OC_scale_edit.h`,
  `OC_visualfx.h`, `util_turing.h`, `util_integer_sequences.h`, `util_math.h`,
  `peaks_bytebeat` all proven portable (DQ/ASR/BBGEN).
- `util/util_logistic_map.h` is header-only pure integer math (no asm, no Teensy),
  compiles as-is, no shim shadow needed.
- `peaks_bytebeat.cpp` is the vendor non-header-only dep (ASR/BYTEBEATGEN already
  link it): `VENDOR_DEPS_QQ := build/arm/vendor_src/peaks_bytebeat.o`,
  `VENDOR_DEP_HOST_SRCS_QQ := $(HEM_SRC_DIR)/peaks_bytebeat.cpp`.
- QQ odr-uses `__popcountdi2` via the integer sequences (ASR already added
  `popcountdi2.c` to `COMPILER_RT_SRCS`).

## Risk: vendor includes HSIOFrame.h

`APP_QQ.h:45` includes `"HSIOFrame.h"` (a Hemisphere header) but a grep finds no
`frame.`/`HS::`/`IOFrame` use in the app body, so it looks vestigial. The shim
`HSIOFrame.h` is hem-coupled and the OC shim-impl does not aggregate the hem
chain, so this MAY not compile in an OC-app TU. Resolution path at first compile:
enumerate with `g++ -ferror-limit=0 -fsyntax-only`; if HSIOFrame drags missing
hem symbols, the fix is include-order (pull whatever it needs from the OC shim
before it) or a guard, NOT a vendor edit. Flag, do not pre-solve.

## Shim additions expected

Reuses everything ASR/DQ already added (full `scale_names`/`scale_names_short`,
the integer-sequence tables, the scale editor symbols, the OC::UI alias,
QuadTitleBar from BBGEN). New work expected to be small: enumerate first-compile
gaps. No new poison shadows expected.

## Out of scope

- Host main-pitch fidelity (the DQ calibrated-DAC vs shim 1V/oct gap; assert the
  ISR -> quantizer -> DAC path runs, defer pitch to hardware smoke).
- Hardware ADD smoke check (GUID candidate `OCQQ`).

## Abort conditions

- More than 2 unforeseen poison shadows: halt.
- Any U16+ setting forced into an int16 param: halt.
- `.text` over the ~82 KB cap: halt (not expected; DQ-shape ~33 KB).
- HSIOFrame needs a vendor edit to compile: halt and reassess.

## Verification footer (per-entry, 3 traced)

- Index 0 `CHANNEL_SETTING_SCALE`: `{ OC::Scales::SCALE_SEMI, 0, OC::Scales::NUM_SCALES - 1,
  "Scale", OC::scale_names_short, STORAGE_TYPE_U8 }`. int16-safe. Exposed (logical 0).
- Index 2 `CHANNEL_SETTING_MASK`: `{ 65535, 1, 65535, "Active notes", NULL,
  STORAGE_TYPE_U16 }` (APP_QQ.h:1163). Overflows int16. Excluded.
- Index 3 `CHANNEL_SETTING_SOURCE`: `{ CHANNEL_SOURCE_CV1, ... }` STORAGE_TYPE_U4.
  int16-safe. Exposed at logical 2 (mask skipped).

3 of 3 consistent with vendor source.
