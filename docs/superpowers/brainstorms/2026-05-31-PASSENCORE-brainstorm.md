# Brainstorm: port PASSENCORE to an NT plug-in

Date: 2026-05-31
Status: accepted
Vendor SHA: 7800d929f25868f9a8b7d3d50514532ee001649b
Issue: #47 (part of the #36 remaining-OC-apps track)

## Scope

Port the vendor O_C app `APP_PASSENCORE.h` (the "Passencore" voice-leading chord
sequencer) to one NT plug-in `.o` under `plugins/apps/`, following the
single-instance OC::App template (Harrington1200 shape). One app, one `.o`.

PASSENCORE walks a target/passing chord engine driven by trigger inputs and
emits a four-voice chord as 1V/oct pitch on the four DAC channels. The chord
voicing logic, the neo-Riemannian/functional scoring, the menu and screensaver
draw code, and the ISR all compile unmodified from the vendor header.

## App shape

- Single file-scope singleton `PASSENCORE passencore_instance`
  (a `settings::SettingsBase<PASSENCORE, PASSENCORE_SETTING_LAST>`) plus a
  separate `PassencoreState passencore_state` holding runtime customUI state
  (the scale editor, the menu cursor, the pending left-encoder scale value).
- Output: pitch. `play_chord()` calls `OC::DAC::set_pitch((DAC_CHANNEL)i, ...)`
  for the four voices. Routed through the shim `pitch_to_dac` (1V/oct), like
  Harrington1200.
- customUI: no long press. The vendor `PASSENCORE_handleButtonEvent` reads only
  `UI::EVENT_BUTTON_PRESS`, so the dispatch flag is `<false>`.

## Settings model (subset, MASK excluded)

18 settings (`PASSENCORE_SETTING_SCALE` .. `PASSENCORE_SETTING_CV4_ROLE`).
17 become flat NT parameter rows. One is excluded:

- `PASSENCORE_SETTING_MASK` (index 1) is `STORAGE_TYPE_U16` with range 1..65535.
  `_NT_parameter` min/max/def and `_NT_algorithm::v` are `int16_t` (max 32767),
  so 65535 cannot be a parameter without truncation and corruption (the FPART
  U32 lesson). It is also a 12-bit scale mask, edited through the vendor scale
  editor customUI, not as a single spinbox. So MASK stays app-internal.

The exposed rows are a non-contiguous subset: MASK sits at index 1 (mid-array),
not at the tail, so the facade remaps logical row `i` to physical setting
`i >= 1 ? i + 1 : i`. `num_settings = PASSENCORE_SETTING_LAST - 1 = 17`.

SCALE (index 0) stays exposed: `PASSENCORE::Loop()` is empty and the runtime's
`loop()` call does not sync scale (the `set_scale(left_encoder_value)` sync
lives only in `PASSENCORE_leftButton`, a customUI button press, and in
`restore`), so an NT SCALE param edit is not clobbered.

Persistence: MASK lives in the same `SettingsBase` whose `Save()` serialises all
18 values regardless of `num_settings`, so the default `make_facade` blob hooks
persist MASK with no override (unlike AUTOMATONNETZ, whose excluded state lived
outside the facade settings).

## Vendor dependencies

App header includes: `util/util_trigger_delay.h`, `braids_quantizer.h`(+cpp),
`braids_quantizer_scales.h`, `OC_scales.h`(+cpp), `OC_scale_edit.h`,
`OC_chords.h`(+cpp), `OC_chords_edit.h`, `OC_input_map.h`(+cpp),
`OC_input_maps.h`.

- `braids_quantizer` + `OC_scales` implementations are already aggregated into
  every OC-app TU by `oc_shim_impl.h` (host and ARM). No extra linkage.
- `OC_chords.cpp` + `OC_input_map.cpp` are vendor non-header-only and must link.
  ARM via `VENDOR_DEPS_PASSENCORE` (`build/arm/vendor_src/OC_chords.o`,
  `OC_input_map.o`); host via `VENDOR_DEP_HOST_SRCS_PASSENCORE`. Neither pulls
  ARM-only asm (`util/util_math.h`, `<arm_math.h>`); they reach only
  `util/util_macros.h` and shadowed `Arduino.h`, so no force-include is needed.
- `util/util_trigger_delay.h`, `braids_quantizer_scales.h`, `OC_chords_edit.h`,
  `OC_input_map.h`, `OC_input_maps.h`, `OC_scale_edit.h` are header-only and
  portable as-is; their risky transitive includes (`Arduino.h`, `OC_DAC.h`,
  `OC_bitmaps.h`, `OC_strings.h`) are all already shadowed.
- `OC_scales.h` is consumed from vendor directly (no shim shadow); its
  `<Arduino.h>` and `"FS.h"` both resolve to shim shadows, proven by
  AUTOMATONNETZ already including it.

No new poison shadows expected.

## Shim additions forecast

- `OC::scale_names`: the shim `OC_scales.cpp` ships a 1-entry `{nullptr}` stub.
  PASSENCORE uses `OC::scale_names[scale]` as the SCALE setting's enum
  `value_names` (so `param_from_value_attr` builds the NT enum across the scale
  range) and in the customUI menu. Port the full vendor 99-entry `scale_names`
  table into the shim `OC_scales.cpp`. This is data, not a vendor edit, and is
  reused by the later scale apps (#48 DQ, #50 QQ, #51 CHORDS).
- `OC::DUMMY`: provided by vendor `OC_scale_edit.h:19` (`const bool DUMMY`),
  which PASSENCORE includes. No shim work.
- Other missing symbols are treated as expected additive shim work, surfaced at
  first compile.

## Out of scope

- The chord editor (`OC_chords_edit.h` `ChordEditor`) is commented out in the
  vendor header, so it is not wired.
- Hardware ADD smoke check (GUID `OCPS`) is deferred to the user (physical
  access).

## Abort conditions

- More than 2 unforeseen poison shadows needed: halt, the boundary is wrong.
- MASK or any U16+ setting forced into an int16 param: halt (corruption).
- `.text` over the ~82 KB scan cap: halt (would need a split; not expected, the
  template apps are 11-30 KB).
