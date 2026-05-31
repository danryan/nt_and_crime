# AUTOMATONNETZ port design

- Vendor pin: `7800d929`. Issue: #46. Brainstorm:
  `docs/superpowers/brainstorms/2026-05-30-AUTOMATONNETZ-brainstorm.md`.
- GUID: `OCAN`. Output: 4-channel 1V/oct tonnetz triad pitch (grid-walked).

## Canonical recipe reference

Single-instance OC::App port, Harrington1200 shape (file-scope `SettingsBase`
singleton `automatonnetz_state`, `oc_runtime::construct(*inst, &the_app,
&automatonnetz_state, GRID_SETTING_LAST)`). Same tonnetz engine as H1200 and the
same pitch-output route (`tonnetz_state.get_outputs` -> `pitch_to_dac`).
`dispatch_custom_ui_factory<true>` (vendor reads `EVENT_BUTTON_LONG_PRESS` on
`CONTROL_BUTTON_L`).

## Deliverables

### 1. Layer 0: shim shadow `shim/include/util/util_sync.h`

Define guard `UTIL_SYNC_H_` (poison) and provide portable `util::CriticalSection`,
`util::TryLock<uint32_t id>`, `util::Lock<uint32_t id>` with a plain non-atomic
`volatile uint32_t id_` lock (no `__LDREXW`/`__STREXW`/`__DMB`/`__CLREX`, no
`<arm_math.h>`). The shim isr cadence runs inside `step()` with no preemptive ISR,
so a non-atomic lock is correct on host and ARM. Same poison technique the
foundation uses for nested vendor includes: the shim header is pulled before the
vendor sibling in the app TU, so the vendor `util_sync.h` self-suppresses. Bodies
mirror the vendor public interface exactly (Init/Unlock/Lock/TryLock + scoped
TryLock/Lock returning `locked()`), only the implementation differs.

### 2. `plugins/apps/AUTOMATONNETZ.cpp`

Harrington1200 template. `#define ENABLE_APP_AUTOMATONNETZ 1` before `#include
"APP_AUTOMATONNETZ.h"`. Build `OC::App` from the vendor `Automatonnetz_*` static
thunks (init, isr, loop, menu, screensaver, handleButtonEvent, handleEncoderEvent,
handleAppEvent, save, restore, storageSize). `_NT_factory` in vendor field order.
`oc_runtime::construct(*inst, &the_app, &automatonnetz_state, GRID_SETTING_LAST)`
(7, the grid subset; cells stay app-internal). `.customUi =
oc_runtime::dispatch_custom_ui_factory<true>`. Render outputs by copying
`automatonnetz_state.tonnetz_state.get_outputs(out)` the same way
`Harrington1200.cpp::render_outputs` does (pitch route). Forward-declare any
free function the vendor header calls before its definition (FPART lesson) only
if first compile demands it.

### 3. `shim/include/oc_app_manifests/AUTOMATONNETZ.h`

GUID `NT_MULTICHAR('O','C','A','N')`, name `"Automatonnetz"`. I/O: CV outs for
the four tonnetz voices (root + 3), trigger ins for clock (`DIGITAL_INPUT_1`) and
reset (`DIGITAL_INPUT_3`, the vendor reset gate), mirror the Harrington1200
manifest I/O block.

### 4. Makefile

`OC_APP_LIST += AUTOMATONNETZ`. `VENDOR_DEPS_AUTOMATONNETZ :=` (empty; tonnetz +
util headers are header-only, confirmed by H1200's empty deps). Host srcs
likewise none.

### 5. `harness/tests/test_oc_app_AUTOMATONNETZ.cpp`

Harrington1200-style: factory/registration (GUID OCAN, 7 grid settings, names
dx/dy/Mode/Oct/TrDly/OutA/Clr), draw, pitch output (clock TR in 1, assert the
four DAC outputs move and stay in pitch range; default major triad is
deterministic), grid-setting blob round-trip, a mutated cell surviving the blob
(persistence covers cells though they are not NT params), parameter-store sync
for the 7 grid rows.

## Spec footer

### Recipe spot-check

Harrington1200 single-instance recipe applied verbatim: singleton `automatonnetz_
state` for `h1200_settings`, `GRID_SETTING_LAST` (7) for the H1200 setting count,
same tonnetz pitch-output route. Divergences from H1200: the util_sync Layer 0
shadow (H1200 does not use util_sync); `dispatch_custom_ui_factory<true>` (H1200
also true, no change); the grid-subset settings decision (H1200 exposes all its
settings; AUTOMATONNETZ exposes 7 of 7 grid + 0 of 100 cell, the cells being a
spatial editor).

### Per-entry verification (3 grid settings traced to vendor source)

1. `GRID_SETTING_DX` (index 0): `SETTINGS_DECLARE` row 0 =
   `{8, 0, 8*GRID_DIMENSION - 1, "dx", NULL, STORAGE_TYPE_I8}` (APP_AUTOMATONNETZ.h:352).
   Range 0..39, default 8. int16-safe. NT row 0.
2. `GRID_SETTING_OCTAVE` (index 3): non-NORTHERNLIGHT branch
   `{0, -3, 3, "Oct", NULL, STORAGE_TYPE_I8}` (APP_AUTOMATONNETZ.h:359). The shim
   does not define NORTHERNLIGHT, so this branch is taken. int16-safe.
3. `GRID_SETTING_OUTPUTMODE` (index 5): `{OUTPUTA_MODE_ROOT, OUTPUTA_MODE_ROOT,
   OUTPUTA_MODE_LAST - 1, "OutA", outputa_mode_names, STORAGE_TYPE_U4}`
   (APP_AUTOMATONNETZ.h:362). Enum via the app-local `outputa_mode_names`
   (defined APP_AUTOMATONNETZ.h:338, no shim work). int16-safe.

### Shim prereq verification

- `util/util_sync.h`: vendor uses ARM CMSIS intrinsics + `<arm_math.h>`,
  not host-portable. MISSING shim shadow -> deliverable 1. Confirmed.
- `util/util_grid.h`: fully self-contained template, no includes. Compiles as-is
  on host and ARM. No work. Confirmed (grep: no `#include`/`arm_`/`__` in file).
- `tonnetz/tonnetz_state.h`+`tonnetz.h`+`tonnetz_abstract_triad.h`: header-only,
  already compiled by H1200, no vendor cpp. No work. Confirmed.
- `OC_trigger_delays.h` (`trigger_delay_ticks`, `Strings::trigger_delay_times`,
  `kNumDelayTimes`): shim-owned, used by H1200. No work. Confirmed.
- `util/util_ringbuffer.h`: vendor, header-only, compiled by H1200. No work.
- `mode_names`/`outputa_mode_names`/`clear_mode_names`/`cell_event_masks`: all
  defined file-scope INSIDE `APP_AUTOMATONNETZ.h` (lines 333/338/346/139), not
  `OC::Strings`. No shim work. Confirmed.
- `tonnetz::transform_names_str`: vendor `tonnetz/tonnetz.h:45`. No work.
- `note_name`: shim-owned (H1200). No work. Confirmed.

Net forecast shim work: ONE file (util_sync shadow). First-compile symbol errors
are mechanical additive work, not boundary defects.
