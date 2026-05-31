# AUTOMATONNETZ port brainstorm

- Vendor pin: `7800d929`. Issue: #46 (6 of 12 in #36). GUID: `OCAN`.
- Status: ready to spec. Single-instance OC::App; pitch output (1V/oct tonnetz triad).

## What AUTOMATONNETZ is

Vector-sequencer driving neo-Riemannian tonnetz transforms. A 5x5 grid of
`TransformCell`; a fixed-point walker (`CellGrid`) steps `(dx, dy)` per clock,
lands on a cell, applies that cell's transform/transpose/inversion to the shared
`TonnetzState`, and outputs a triad (root + three voices) as 1V/oct pitch on the
four DAC channels. Same tonnetz engine as Harrington 1200 (#H1200), which is
already ported.

## Vendor trace (`APP_AUTOMATONNETZ.h`, 745 lines)

- App object: file-scope `AutomatonnetzState automatonnetz_state;`, a
  `settings::SettingsBase<AutomatonnetzState, GRID_SETTING_LAST>` (singleton ->
  Harrington1200 single-instance template, NOT the BBGEN quad facade).
- `GRID_SETTING_LAST == 7`: dx, dy (I8 0..39), Mode (U8), Oct (I8 -3..3), TrDly
  (U8 trigger-delay index), OutA (U4), Clr (U4). All int16-safe.
- Held alongside the 7 grid settings: `TransformCell cells_[25]`, each a
  `SettingsBase<TransformCell, CELL_SETTING_LAST>` of 4 settings (Trfm U8, Offs
  I8 -12..12, Inv I8 -3..3, Muta U8 0..7), plus `CellGrid grid`, `TonnetzState
  tonnetz_state`, a `ui` struct (two cursors + history ring).
- Output: `Automatonnetz_isr` -> `AutomatonnetzState::ISR` walks the grid on a
  TR clock, writes `tonnetz_state.get_outputs(out)` (four 1V/oct pitches) to DAC
  A-D. Pitch app -> `pitch_to_dac` / `set_voltage_scaled_semitone`, NOT raw
  modulation codes.
- customUI: `Automatonnetz_handleButtonEvent` / `handleEncoderEvent` drive the
  grid/cell cursors. Long-press usage: confirm against `EVENT_BUTTON_LONG_PRESS`
  at spec time (sets the `dispatch_custom_ui_factory<bool>` flag).

## Settings-model decision (user-approved): grid subset, 7 NT params

Expose only the 7 global GRID settings as NT parameters. The 25 cells (100
settings) stay app-internal, edited through the vendor 2D grid customUI, and
persisted whole via the `Save/Restore` blob. Rationale: cells are a spatial
editor (`CellGrid` + cursor); they do not linearize into a flat NT param page
meaningfully, and 100 ungrouped rows would swamp it. Mirrors the FPART subset
shape, but the trigger here is UX/structure, not int16 overflow (every cell
setting IS int16-safe; this is a deliberate UX subset). Pass `7`
(`GRID_SETTING_LAST`), NOT the cell total, as `num_settings` to
`oc_runtime::construct`. The blob path is independent of `num_settings` (it calls
vendor `SettingsBase::Save` for the whole object), so cell persistence is
unaffected.

## Layer 0 (this port builds)

1. Shim shadow `shim/include/util/util_sync.h` poisoning `UTIL_SYNC_H_`. Vendor
   `util/util_sync.h` uses ARM CMSIS intrinsics (`__LDREXW`/`__STREXW`/`__DMB`/
   `__CLREX`) and `#include <arm_math.h>`, neither host-portable. Provide a
   portable `util::CriticalSection` + `util::TryLock<id>` + `util::Lock<id>` with
   a plain non-atomic lock. The NT shim has no preemptive ISR racing the main
   loop (the isr cadence runs inside `step()`), so a non-atomic lock is correct
   on both host and ARM. This is the single forecast Layer 0 stub.

## Already proven (no work)

- `tonnetz/tonnetz_state.h` + `tonnetz.h` + `tonnetz_abstract_triad.h`: vendor,
  header-only, already compiled by H1200; no shadow, no vendor cpp dep
  (VENDOR_DEPS_Harrington1200 is empty).
- `OC_trigger_delays.h` (`OC::trigger_delay_ticks`, `OC::Strings::
  trigger_delay_times`, `kNumDelayTimes`): shim-owned, used by H1200.
- `util/util_ringbuffer.h`: vendor, header-only, compiled by H1200.
- `util/util_grid.h` (`vec2`, `CellGrid`): fully self-contained template (no
  includes), portable as-is on host and ARM. No shadow.

## Additive shim strings (expected, enumerate at first compile)

`OC::Strings`: `mode_names`, `outputa_mode_names`, `clear_mode_names`,
`cell_event_masks`, `tonnetz::transform_names_str`. Some may already exist from
H1200. Treat first-compile missing-symbol errors as mechanical additive shim work
(BBGEN lesson). Enumerate the complete set with `g++ -ferror-limit=0
-fsyntax-only` before hand-porting.

## Vendor `.cpp` deps

Expect none (`VENDOR_DEPS_AUTOMATONNETZ` empty), same as H1200: tonnetz and the
util headers are header-only. Confirm at ARM link with `arm-none-eabi-nm` (no
unresolved `tonnetz::`/`CellGrid` symbols).

## Test focus

Harrington1200-style single-instance: factory/registration (GUID OCAN, 7 grid
settings), draw, pitch output (clock the grid via TR, assert DAC A-D move and
stay in the +-5V pitch range; the default major triad gives a deterministic root),
grid-setting round-trip through the blob (and confirm a mutated cell survives the
blob even though it is not an NT param), parameter-store sync for the 7 grid rows.

## Scope / aborts

In scope: AUTOMATONNETZ `.cpp`, manifest, the util_sync shadow, any additive
strings, Makefile entry, host test. Abort if the shim prereq list far exceeds the
forecast single util_sync shadow plus strings (signals a mis-drawn boundary), or
if util_grid/tonnetz need a non-shadowable Teensy header on the host.
