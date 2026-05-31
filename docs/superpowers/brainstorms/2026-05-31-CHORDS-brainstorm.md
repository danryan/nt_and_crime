# Brainstorm: CHORDS (Chords) NT plug-in

Date: 2026-05-31
Status: shipped
Vendor SHA: 7800d929f25868f9a8b7d3d50514532ee001649b
Issue: #51

## Scope

Port vendor `APP_CHORDS.h` (Chords: a four-voice chord sequencer) to one NT
plug-in `.o` under `plugins/apps/`, on the shared branch `dr/oc-apps-port-2`.

## Categorization

Single-instance app (the file-scope `chords` singleton, a
`SettingsBase<Chords, CHORDS_SETTING_LAST>`, plus a separate `chords_state`
holding the customUI state). This is the PASSENCORE template:

- One U16 setting overflows int16 and is excluded mid-array:
  `CHORDS_SETTING_MASK` (index 3, range 1..65535), the 12-bit scale mask edited
  through the scale editor. Facade remaps logical row i to physical
  `i >= 3 ? i + 1 : i`. `CHORDS_SETTING_LAST == 31`, so 30 exposed NT rows.
- The mask lives INSIDE the `chords` SettingsBase, so the default
  `make_facade(&chords)` blob (`Chords::Save` serialises all 31 values) persists
  it. No whole-app blob override needed (unlike AUTOMATONNETZ / DQ / QQ, whose
  channels lived in a separate array).
- Two vendor UI editors: `OC::ScaleEditor<Chords>` AND `OC::ChordEditor<Chords>`
  (PASSENCORE had only the scale editor). Both, plus the app handleButtonEvent,
  read `EVENT_BUTTON_LONG_PRESS`, so dispatch is `<true>`.

## Confirmed clean of the HSIOFrame blocker

`APP_CHORDS.h` does NOT `#include "HSIOFrame.h"` (the QQ/SEQ provisioning-patch
case). Includes are all OC headers (OC_apps, OC_menus, OC_scales, OC_scale_edit,
OC_chords, OC_chords_edit, OC_input_map[s], util_settings, util_trigger_delay,
braids_quantizer[_scales]). No vendor patch needed.

## Risks (all resolved)

- Chord-shape menu widgets: `menu::DrawChord` / `menu::DrawMiniChord` were dropped
  from the foundation shim `OC_menus.h`; CHORDS uses both. Re-add (they read the
  vendor chord-presets data).
- `OC::Strings::seq_directions` (the Direction enum labels) was not shim-owned.
- Fidelity limit: user chord-slot definitions live in `OC::user_chords[]`, NOT in
  the `chords` SettingsBase, and the vendor app blob does not serialise them
  either (global EEPROM on hardware). The NT port matches the vendor blob; chord
  edits do not survive a preset reload. Documented, not a defect.

## Exclusions

None beyond the U16 mask subset.

## Out of scope

SEQ (issue #52, the capstone; shares the HSIOFrame include with QQ, unblocked by
the existing `vendor-patches/` patch).
