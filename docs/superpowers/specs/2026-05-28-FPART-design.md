# FPART (APP_FPART) O_C app port: design

- Vendor pin: `7800d929`
- Issue: #41. Foundation: #29. Track: #36 (position 1 of 12).
- Recipe class: O_C full-screen app, file-scope `SettingsBase` singleton,
  long-press button event (the Harrington1200 variant of the Low_rents recipe).

## Canonical recipe reference

`plugins/apps/Harrington1200.cpp` is the reference: it owns a file-scope
`SettingsBase` singleton and maps a vendor `EVENT_BUTTON_LONG_PRESS`. FPART
follows it structurally. The per-app runtime
(`plugins/apps/_per_app_runtime.h`) is used unedited except for the Layer 0
`kMaxBlobBytes` bump.

Per-app `.cpp` structure (FPART deviations called out):

1. `#define NT_OC_APP_TU 1` then `#include "_per_app_runtime.h"` (aggregates the
   OC shim impl into this single TU).
2. Bare-name shim includes (resolve to `shim/include/` via `-Ishim/include`).
3. `namespace menu = OC::menu;` (bind bare `menu::` without dragging `OC::UI`).
4. Forward-declare the six vendor button-helper free functions
   (`FPART_upButton`, `FPART_downButton`, `FPART_leftButton`,
   `FPART_rightButton`, `FPART_downButtonLong`, `FPART_leftButtonLong`).
   FPART-specific: the vendor `FPART_handleButtonEvent` calls these before their
   definitions; the vendor relies on the Arduino IDE's auto-prototype pass, which
   this build does not have. Declaring them before the include is the fix; the
   vendor source is not edited.
5. `#define ENABLE_APP_FPART 1` then `#include "APP_FPART.h"`.
6. Build the `OC::App` aggregate from the `FPART_*` thunks in vendor field order;
   bridge the two event-handler pointers with `reinterpret_cast<OcEventFn>`.
7. `calculateRequirements`: `numParameters = kIoParamCount + kFpartHeadParams`
   (12 + 10). FPART-specific: the count is the head-setting count (10), NOT
   `FPART_SETTING_LAST`.
8. `construct`: `oc_runtime::construct(*inst, &the_fpart_app, &fpart_instance,
   kFpartHeadParams)`. The facade's save/restore still cover all 109 settings;
   only `num_settings` (the parameter-table extent and push-back range) is the
   head count.
9. customUI: the Harrington1200 long-press mapping (`LONG_RELEASE` classification
   -> vendor `EVENT_BUTTON_LONG_PRESS`); encoders emit `ENCODER_L/R`; push-back
   mirrors changed head settings into `alg->v` with `NT_parameterOffset()`.
10. `_NT_factory` in vendor field order (tags before hasCustomUi/customUi).
11. Test seams (full 109-setting access, chord helpers, sentinel, encoder edit).

## Layer 0 (shared work)

Raise `oc_runtime::kMaxBlobBytes` from 256 to 512 in
`plugins/apps/_per_app_runtime.h`. Reason: the FPART `SettingsBase::Save` blob is
406 bytes; the 256 bound made `serialise()`/`deserialise()` bail at the
`storage_size() > kMaxBlobBytes` guard. `kMaxSettings` (64) is unchanged: only 10
parameters are exposed.

## Parameter exposure decision

| Settings | Type | NT param? | Edit path | Persist |
| --- | --- | --- | --- | --- |
| 0..9 (head) | U8/I8, int16-safe | yes (10 rows) | param page + staff page | blob |
| 10..108 (chords) | U32 (10101010..32323232) | no | staff-page customUI only | blob |

The chord ints exceed int16 range, so they are not parameters. The full 109-row
`Save/Restore` blob persists everything.

## I/O routing (manifest documentation; runtime emits generic row names)

- CV in 1: absolute chord select (ADC1). CV in 2: in-loop select (ADC2).
  CV in 3/4: unused by the vendor app.
- CV out A-D: the four voices' 1V/oct pitch.
- TR in 1-4: step back, step forward, jump to loop start, jump to loop end.

## GUID

`OCFP` (`NT_MULTICHAR('O','C','F','P')`). Unique against shipped OCLR, OCHA, OCSb.

## Test coverage (`harness/tests/test_oc_app_FPART.cpp`)

- factory path + GUID + 109 setting count + 10 head-param count.
- draw renders the staff page (default view), non-zero pixels, suppress == true.
- large settings-table round-trip: 10 head + 5 chords (slots 0, 1, 50, 97, 98)
  set to non-defaults, serialise, clobber, deserialise, verify all restored.
  This is the `kMaxBlobBytes`-bump regression.
- isr pitch output: voice A note 0 at root C / Ionian lands on 5504/1536 V;
  a +1 octave-setting step shifts the routed CV-out by exactly +1.0 V (1V/oct).
- NT parameter bidirectional sync over the head settings (both directions).
- customUI push-back honors `NT_parameterOffset()` (edits the target row only).

## 10x-multiplier / one-edge-per-tick disposition

FPART runs on the OC per-app runtime, not the Hemisphere 10x clocked path. The
runtime's `step()` uses the isr cadence accumulator plus the one-edge-per-tick
`DigitalInputs::Scan` discipline. The pitch test leaves the input buses silent so
the trigger/CV chord-stepping does not perturb the active chord; the assertion
is on the held pitch output, not on a per-edge fire count.

## Spec footer

### Recipe spot-check

The `OC::App` aggregate field order, the `reinterpret_cast` event bridge, the
facade wiring via `oc_runtime::construct`, the `NT_parameterOffset()` push-back,
and the `_NT_factory` field order all match Harrington1200.cpp verbatim. The two
deviations (head-only `num_settings`; vendor button-helper forward declarations)
are documented inline in `FPART.cpp` and above.

### Per-entry verification (3 settings traced end-to-end against APP_FPART.h)

- FPART_SETTING_ROOT (idx 0): SETTINGS_DECLARE line 428 `{0,0,11,"tonic",
  note_names_unpadded, U8}`. int16-safe. Exposed as NT enum param (enumStrings =
  note_names_unpadded + 0). Verified.
- FPART_SETTING_D_OCTAVE (idx 7): line 435 `{0,-3,6,"chan d oct",NULL,I8}`.
  int16-safe. Numeric param. ISR uses `get_d_oct()` as the octave offset to
  `set_pitch(DAC_CHANNEL_D, ...)`. Verified.
- FPART_SETTING_CHORD50 (idx 60): line 488 `{10101010,10101010,32323232,
  "chord50 int",NULL,U32}`. Range exceeds int16: NOT a parameter. Edited via
  `set_chord_int(50, build_chord_int(...))`, persisted in the blob. The
  round-trip test writes slot 50 and verifies restore. Verified.

Result: 0 of 3 contradict the source. The chord-int range finding is consistent
across all 99 chord entries (identical SETTINGS_DECLARE rows 438-536).

### Shim prereq verification

All symbols FPART references exist in the shim before this port: graphics
methods (drawCircle/drawFrame/drawRect/drawLine/drawVLine/setPrintPos/print),
menu widgets (ScreenCursor, kScreenLines, DefaultTitleBar, SettingsList,
SettingsListItem, kDefaultValueX), `OC::Strings::note_names_unpadded`,
`OC::DAC::set_pitch` + the 16-bit code constants, `OC::ADC::raw_pitch_value`,
`OC::DigitalInputs::clocked`, `constrain`. New shim work for this port: the
include-guard poison added to `OC_menus.h`, `OC_apps.h`, `OC_DAC.h`, `OC_ADC.h`
(FPART is the first app to quote-include those vendor siblings from inside a
vendor app header).
