# Design: CHORDS (Chords) NT plug-in

Date: 2026-05-31
Status: implemented
Vendor SHA: 7800d929f25868f9a8b7d3d50514532ee001649b
Issue: #51

## Recipe

Single-instance OC-app port, PASSENCORE template (mid-array U16 subset, mask
inside the SettingsBase, default blob). Two vendor UI editors.

- `plugins/apps/CHORDS.cpp`: `NT_OC_APP_TU`; `ui_events.h` FIRST (both editors do
  member access on `UI::Event` inside namespace OC -> the OC::UI alias);
  `MENU_REDRAW`; `ENABLE_APP_CHORDS`; the `OC::App` from the `CHORDS_*` thunks;
  facade = `make_facade(&chords)` with get/apply/value_attr overridden by the
  MASK-skipping remap (`phys_setting(i) = i >= 3 ? i + 1 : i`); save/restore/
  storage_size keep the defaults; `dispatch_custom_ui_factory<true>`. GUID `OCCH`.
- `shim/include/oc_app_manifests/CHORDS.h`: 4 CV in, 4 CV out (Voice A..D), 4 trig
  in (Advance / Reset / S+H-CV / Aux).
- `harness/tests/test_oc_app_CHORDS.cpp`: 7 cases (factory/registration, subset
  remap, draw, trigger-driven output-smoke, settings+mask round-trip, param-sync
  both directions, offset push-back).

## Settings model

`CHORDS_SETTING_LAST == 31`. `CHORDS_SETTING_MASK` (index 3) is the only U16,
excluded; 30 exposed NT rows. `numParameters = kIoParamCount + 30`. The mask
persists through the default `make_facade(&chords)` blob (`Chords::Save`
serialises all 31).

## Shim additions

- `OC::menu::DrawChord` / `DrawMiniChord` (vendor `OC_menus.h:151,178`), re-added
  to the shim `OC_menus.h`. They read vendor chord-presets data, so the shadow now
  `#include "OC_chords.h"` (NOT `OC_chords_presets.h`: the two are mutually
  recursive, and including `OC_chords.h` first resolves `struct Chord` before
  `GetChord`). Inline + uncalled in non-chord apps, so zero link cost there.
- `OC::Strings::seq_directions` (vendor `OC_strings.cpp:57`,
  `{"fwd","rev","pnd1","pnd2","rnd","brwn"}`): decl in shim `OC_strings.h`, def in
  `globals.cpp`.
- `VENDOR_DEPS_CHORDS` = `OC_chords.o` + `OC_input_map.o` (same as PASSENCORE).

## Verification footer

- Recipe spot-check: built host (7/7, 81 assertions) and ARM (`.text` 34275 B,
  `nm` clean: only firmware-provided symbols unresolved).
- Per-entry verification (3 traced against `APP_CHORDS.h`):
  - `CHORDS_SETTING_MASK` index = 3, `STORAGE_TYPE_U16`, range 1..65535
    (`SETTINGS_DECLARE` line 1009). Matches: excluded, default 65535 asserted.
  - `CHORDS_SETTING_LAST` = 31 (enum SCALE..MORE_DUMMY, line 44-77). Matches: 30
    exposed.
  - The cursor list `enabled_settings_[0]` = `CHORDS_SETTING_MASK`,
    `[1]` = `CHORDS_SETTING_ROOT` (`update_enabled_settings`, MENU_PARAMETERS).
    Matches: the param-sync direction-2 test scrolls 1 step off MASK to ROOT
    before toggling editing.
- Shim prereq verification: `seq_directions`, `DrawChord`, `DrawMiniChord` are the
  complete first-compile gap set (`g++ -ferror-limit=0 -fsyntax-only`); the
  `value_attr` incomplete-type error was the ENVGEN red herring, cleared by
  `seq_directions`.
